/*
 * Copyright (c) 2019, Intel Corporation
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * * Redistributions of source code must retain the above copyright notice, this
 *   list of conditions and the following disclaimer.
 * * Redistributions in binary form must reproduce the above copyright notice,
 *   this list of conditions and the following disclaimer in the documentation
 *   and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

//!
//! \file:   ExtractorTrackGenerator.cpp
//! \brief:  Extractor track generator class implementation
//!
//! Created on April 30, 2019, 6:04 AM
//!

#include <set>
#include <math.h>

#include "ExtractorTrackGenerator.h"
#include "VideoStream.h"

#ifdef _USE_TRACE_
#include "../trace/Bandwidth_tp.h"
#endif

VCD_NS_BEGIN

ExtractorTrackGenerator::~ExtractorTrackGenerator()
{
    if (m_tilesSelection.size())
    {
        std::map<uint16_t, std::map<uint16_t, TileDef*>>::iterator it;
        for (it = m_tilesSelection.begin(); it != m_tilesSelection.end(); )
        {
            std::map<uint16_t, TileDef*> oneLayout = it->second;
            std::map<uint16_t, TileDef*>::iterator it1;
            for (it1 = oneLayout.begin(); it1 != oneLayout.end(); )
            {
                TileDef *oneTile = it1->second;
                DELETE_ARRAY(oneTile);
                oneLayout.erase(it1++);
            }
            oneLayout.clear();
            m_tilesSelection.erase(it++);
        }

        m_tilesSelection.clear();
    }

    if (m_viewportCCInfo.size())
    {
        std::map<uint16_t, CCDef*>::iterator it;
        for (it = m_viewportCCInfo.begin(); it != m_viewportCCInfo.end(); )
        {
            CCDef *oneCC = it->second;
            DELETE_MEMORY(oneCC);
            m_viewportCCInfo.erase(it++);
        }
        m_viewportCCInfo.clear();
    }

    if (m_rwpkGenMap.size())
    {
        std::map<uint16_t, RegionWisePackingGenerator*>::iterator it;
        for (it = m_rwpkGenMap.begin(); it != m_rwpkGenMap.end(); )
        {
            RegionWisePackingGenerator *rwpkGen = it->second;
            DELETE_MEMORY(rwpkGen);
            m_rwpkGenMap.erase(it++);
        }
        m_rwpkGenMap.clear();
    }

    DELETE_ARRAY(m_videoIdxInMedia);
    if (m_newSPSNalu)
    {
        DELETE_ARRAY(m_newSPSNalu->data);
    }
    DELETE_MEMORY(m_newSPSNalu);
    if (m_newPPSNalu)
    {
        DELETE_ARRAY(m_newPPSNalu->data);
    }
    DELETE_MEMORY(m_newPPSNalu);
    m_360scvpParam = NULL;
    m_360scvpHandle = NULL;
}

int32_t ExtractorTrackGenerator::SelectTilesInView(
    float yaw, float pitch,
    uint8_t tileInRow, uint8_t tileInCol)
{
    if (!m_360scvpParam || !m_360scvpHandle)
    {
        OMAF_LOG(LOG_ERROR, "360SCVP should be set up before selecting tiles based on viewport !\n");
        return OMAF_ERROR_NULL_PTR;
    }

    if ((yaw < -180.0) || (yaw > 180.0))
    {
        OMAF_LOG(LOG_ERROR, "Invalid yaw in selecting tiles based on viewport !\n");
        return OMAF_ERROR_INVALID_DATA;
    }

    if ((pitch < -90.0) || (pitch > 90.0))
    {
        OMAF_LOG(LOG_ERROR, "Invalid pitch in selecting tiles based on viewport !\n");
        return OMAF_ERROR_INVALID_DATA;
    }

    int32_t ret = I360SCVP_setViewPort(m_360scvpHandle, yaw, pitch);
    if (ret)
    {
        OMAF_LOG(LOG_ERROR, "Failed to set viewport !\n");
        return OMAF_ERROR_SCVP_SET_FAILED;
    }

    ret = I360SCVP_process(m_360scvpParam, m_360scvpHandle);
    if (ret)
    {
        OMAF_LOG(LOG_ERROR, "Failed in 360SCVP process !\n");
        return OMAF_ERROR_SCVP_PROCESS_FAILED;
    }

    uint64_t totalTiles = tileInRow * tileInCol;
    TileDef *tilesInView = new TileDef[1024];
    if (!tilesInView)
    {
        OMAF_LOG(LOG_ERROR, "Failed to create tiles def array !\n");
        return OMAF_ERROR_NULL_PTR;
    }

    memset(tilesInView, 0, 1024 * sizeof(TileDef));

    Param_ViewportOutput paramViewport;
    int32_t selectedTilesNum = 0;
    selectedTilesNum = I360SCVP_getTilesInViewport(tilesInView, &paramViewport, m_360scvpHandle);
    if ((selectedTilesNum <= 0) || ((uint64_t)(selectedTilesNum) > totalTiles))
    {
        OMAF_LOG(LOG_ERROR, "Unreasonable selected tiles number based on viewport !\n");
        delete [] tilesInView;
        tilesInView = NULL;
        return OMAF_ERROR_SCVP_INCORRECT_RESULT;
    }

    uint32_t sqrtedSize = (uint32_t)sqrt(selectedTilesNum);
    while(sqrtedSize && (selectedTilesNum % sqrtedSize)) { sqrtedSize--; }
    if (sqrtedSize == 1)
    {
        OMAF_LOG(LOG_INFO, "Additional tile needs to be selected for tiles stitching !\n");
        selectedTilesNum++;
        tilesInView[selectedTilesNum-1].x = tilesInView[0].x;
        tilesInView[selectedTilesNum-1].y = tilesInView[0].y;
        tilesInView[selectedTilesNum-1].idx = tilesInView[0].idx;
        tilesInView[selectedTilesNum-1].faceId = tilesInView[0].faceId;
    }

    //adjust selected tiles number again to make sure packed sub-picture width/height ratio to a normal range
    sqrtedSize = (uint32_t)sqrt(selectedTilesNum);
    while(sqrtedSize && (selectedTilesNum % sqrtedSize)) { sqrtedSize--; }
    uint32_t dividedSize = selectedTilesNum / sqrtedSize;
    uint32_t supplementedNum = 0;

    if (((sqrtedSize > dividedSize) && ((sqrtedSize - dividedSize) > 3)) ||
        ((dividedSize > sqrtedSize) && ((dividedSize - sqrtedSize) > 3)))
    {
        OMAF_LOG(LOG_INFO, "High packed sub-picture width/height ratio %u : %u\n", (sqrtedSize > dividedSize ? sqrtedSize : dividedSize), (sqrtedSize > dividedSize ? dividedSize : sqrtedSize));
    }

    while ((sqrtedSize > dividedSize) && ((sqrtedSize - dividedSize) > 3))
    {
        selectedTilesNum++;
        supplementedNum++;
        tilesInView[selectedTilesNum-1].x = tilesInView[supplementedNum].x;
        tilesInView[selectedTilesNum-1].y = tilesInView[supplementedNum].y;
        tilesInView[selectedTilesNum-1].idx = tilesInView[supplementedNum].idx;
        tilesInView[selectedTilesNum-1].faceId = tilesInView[supplementedNum].faceId;
        sqrtedSize = (uint32_t)sqrt(selectedTilesNum);
        while(sqrtedSize && (selectedTilesNum % sqrtedSize)) { sqrtedSize--; }
        if (sqrtedSize == 1)
        {
            selectedTilesNum++;
            supplementedNum++;
            tilesInView[selectedTilesNum-1].x = tilesInView[supplementedNum].x;
            tilesInView[selectedTilesNum-1].y = tilesInView[supplementedNum].y;
            tilesInView[selectedTilesNum-1].idx = tilesInView[supplementedNum].idx;
            tilesInView[selectedTilesNum-1].faceId = tilesInView[supplementedNum].faceId;
            sqrtedSize = (uint32_t)sqrt(selectedTilesNum);
            while(sqrtedSize && (selectedTilesNum % sqrtedSize)) { sqrtedSize--; }
        }

        dividedSize = selectedTilesNum / sqrtedSize;
    }
    while (( dividedSize > sqrtedSize) && ((dividedSize - sqrtedSize) > 3))
    {
        selectedTilesNum++;
        supplementedNum++;
        tilesInView[selectedTilesNum-1].x = tilesInView[supplementedNum].x;
        tilesInView[selectedTilesNum-1].y = tilesInView[supplementedNum].y;
        tilesInView[selectedTilesNum-1].idx = tilesInView[supplementedNum].idx;
        tilesInView[selectedTilesNum-1].faceId = tilesInView[supplementedNum].faceId;
        sqrtedSize = (uint32_t)sqrt(selectedTilesNum);
        while(sqrtedSize && (selectedTilesNum % sqrtedSize)) { sqrtedSize--; }
        if (sqrtedSize == 1)
        {
            selectedTilesNum++;
            supplementedNum++;
            tilesInView[selectedTilesNum-1].x = tilesInView[supplementedNum].x;
            tilesInView[selectedTilesNum-1].y = tilesInView[supplementedNum].y;
            tilesInView[selectedTilesNum-1].idx = tilesInView[supplementedNum].idx;
            tilesInView[selectedTilesNum-1].faceId = tilesInView[supplementedNum].faceId;
            sqrtedSize = (uint32_t)sqrt(selectedTilesNum);
            while(sqrtedSize && (selectedTilesNum % sqrtedSize)) { sqrtedSize--; }
        }

        dividedSize = selectedTilesNum / sqrtedSize;
    }

    if (supplementedNum > 0)
    {
        OMAF_LOG(LOG_INFO, "Supplement %u tiles for packed sub-picture width/height ratio\n", supplementedNum);

        OMAF_LOG(LOG_INFO, "Now packed sub-picture width/height ratio %u : %u\n", (dividedSize > sqrtedSize ? dividedSize : sqrtedSize), (dividedSize > sqrtedSize ? sqrtedSize : dividedSize));
    }

    CCDef *outCC = new CCDef;
    if (!outCC)
    {
        delete [] tilesInView;
        tilesInView = NULL;
        return OMAF_ERROR_NULL_PTR;
    }
    ret = I360SCVP_getContentCoverage(m_360scvpHandle, outCC);
    if (ret)
    {
        OMAF_LOG(LOG_ERROR, "Failed to calculate Content coverage information !\n");
        delete [] tilesInView;
        tilesInView = NULL;
        delete outCC;
        outCC = NULL;
        return OMAF_ERROR_SCVP_INCORRECT_RESULT;
    }

    std::map<uint16_t, std::map<uint16_t, TileDef*>>::iterator it;
    it = m_tilesSelection.find((uint16_t)selectedTilesNum);
    if (it == m_tilesSelection.end())
    {
        std::map<uint16_t, TileDef*> oneLayout;
        oneLayout.insert(std::make_pair(m_viewportNum, tilesInView));
        m_tilesSelection.insert(std::make_pair((uint16_t)selectedTilesNum, oneLayout));
        m_viewportCCInfo.insert(std::make_pair(m_viewportNum, outCC));
        m_viewportNum++;
    }
    else
    {
        std::map<uint16_t, TileDef*>* oneLayout = &(it->second);
        std::map<uint16_t, TileDef*>::iterator it1;
        uint64_t diffNum = 0;
        for (it1 = oneLayout->begin(); it1 != oneLayout->end(); it1++)
        {
            TileDef *oneTilesSet = it1->second;
            if (!oneTilesSet)
            {
                DELETE_ARRAY(tilesInView);
                return OMAF_ERROR_NULL_PTR;
            }

            uint16_t i = 0;
            for ( ; i < selectedTilesNum; i++)
            {
                TileDef *oneTile = &(tilesInView[i]);
                if (!oneTile)
                {
                    DELETE_ARRAY(tilesInView);
                    return OMAF_ERROR_NULL_PTR;
                }

                uint16_t j = 0;
                for ( ; j < selectedTilesNum; j++)
                {
                    TileDef *tile = &(oneTilesSet[j]);
                    if ((oneTile->x == tile->x) && (oneTile->y == tile->y) &&
                        (oneTile->idx == tile->idx) && (oneTile->faceId == tile->faceId))
                    {
                        break;
                    }
                }
                if (j == selectedTilesNum)
                    break;
            }
            if (i < selectedTilesNum )
            {
                diffNum++;
            }
        }
        if (diffNum == oneLayout->size())
        {
            oneLayout->insert(std::make_pair(m_viewportNum, tilesInView));
            m_viewportCCInfo.insert(std::make_pair(m_viewportNum, outCC));
            m_viewportNum++;
        }
        else
        {
            DELETE_ARRAY(tilesInView);
            DELETE_MEMORY(outCC);
        }
    }

    return ERROR_NONE;
}

int32_t ExtractorTrackGenerator::CalculateViewportNum()
{
    if (!m_videoIdxInMedia)
        return OMAF_ERROR_NULL_PTR;

    std::map<uint8_t, MediaStream*>::iterator it;
    it = m_streams->find(m_videoIdxInMedia[0]);
    if (it == m_streams->end())
        return OMAF_ERROR_STREAM_NOT_FOUND;

    VideoStream *vs = (VideoStream*)(it->second);
    uint16_t origWidth  = vs->GetSrcWidth();
    uint16_t origHeight = vs->GetSrcHeight();
    uint8_t  tileInRow  = vs->GetTileInRow();
    uint8_t  tileInCol  = vs->GetTileInCol();

    m_360scvpParam = new param_360SCVP;
    if (!m_360scvpParam)
    {
        OMAF_LOG(LOG_ERROR, "Failed to create 360SCVP parameter !\n");
        return OMAF_ERROR_NULL_PTR;
    }

    m_360scvpParam->usedType = E_VIEWPORT_ONLY;
    m_360scvpParam->logFunction = (void*)logCallBack;
    if (m_initInfo->projType == E_SVIDEO_EQUIRECT) {
        m_360scvpParam->paramViewPort.viewportWidth = (m_initInfo->viewportInfo)->viewportWidth;
        m_360scvpParam->paramViewPort.viewportHeight = (m_initInfo->viewportInfo)->viewportHeight;
        m_360scvpParam->paramViewPort.viewPortPitch = (m_initInfo->viewportInfo)->viewportPitch;
        m_360scvpParam->paramViewPort.viewPortYaw = (m_initInfo->viewportInfo)->viewportYaw;
        m_360scvpParam->paramViewPort.viewPortFOVH = (m_initInfo->viewportInfo)->horizontalFOVAngle;
        m_360scvpParam->paramViewPort.viewPortFOVV = (m_initInfo->viewportInfo)->verticalFOVAngle;
        m_360scvpParam->paramViewPort.geoTypeInput = (EGeometryType)(m_initInfo->projType);
        m_360scvpParam->paramViewPort.geoTypeOutput = E_SVIDEO_VIEWPORT;
        m_360scvpParam->paramViewPort.tileNumRow = tileInCol;
        m_360scvpParam->paramViewPort.tileNumCol = tileInRow;
        m_360scvpParam->paramViewPort.usageType = E_VIEWPORT_ONLY;
        m_360scvpParam->paramViewPort.faceWidth = origWidth;
        m_360scvpParam->paramViewPort.faceHeight = origHeight;
        m_360scvpParam->paramViewPort.paramVideoFP.cols = 1;
        m_360scvpParam->paramViewPort.paramVideoFP.rows = 1;
        m_360scvpParam->paramViewPort.paramVideoFP.faces[0][0].idFace = 0;
        m_360scvpParam->paramViewPort.paramVideoFP.faces[0][0].rotFace = NO_TRANSFORM;
    } else if (m_initInfo->projType == E_SVIDEO_CUBEMAP) {
        m_360scvpParam->paramViewPort.viewportWidth = (m_initInfo->viewportInfo)->viewportWidth;
        m_360scvpParam->paramViewPort.viewportHeight = (m_initInfo->viewportInfo)->viewportHeight;
        m_360scvpParam->paramViewPort.viewPortPitch = (m_initInfo->viewportInfo)->viewportPitch;
        m_360scvpParam->paramViewPort.viewPortYaw = (m_initInfo->viewportInfo)->viewportYaw;
        m_360scvpParam->paramViewPort.viewPortFOVH = (m_initInfo->viewportInfo)->horizontalFOVAngle;
        m_360scvpParam->paramViewPort.viewPortFOVV = (m_initInfo->viewportInfo)->verticalFOVAngle;
        m_360scvpParam->paramViewPort.geoTypeInput = (EGeometryType)(m_initInfo->projType);
        m_360scvpParam->paramViewPort.geoTypeOutput = E_SVIDEO_VIEWPORT;
        m_360scvpParam->paramViewPort.tileNumRow = tileInCol / 2;
        m_360scvpParam->paramViewPort.tileNumCol = tileInRow / 3;
        m_360scvpParam->paramViewPort.usageType = E_VIEWPORT_ONLY;
        m_360scvpParam->paramViewPort.faceWidth = origWidth / 3;
        m_360scvpParam->paramViewPort.faceHeight = origHeight / 2;

        m_360scvpParam->paramViewPort.paramVideoFP.cols = 3;
        m_360scvpParam->paramViewPort.paramVideoFP.rows = 2;
        m_360scvpParam->paramViewPort.paramVideoFP.faces[0][0].idFace = 0;
        m_360scvpParam->paramViewPort.paramVideoFP.faces[0][0].rotFace = NO_TRANSFORM;
        m_360scvpParam->paramViewPort.paramVideoFP.faces[0][0].faceWidth = m_360scvpParam->paramViewPort.faceWidth;
        m_360scvpParam->paramViewPort.paramVideoFP.faces[0][0].faceHeight = m_360scvpParam->paramViewPort.faceHeight;
        m_360scvpParam->paramViewPort.paramVideoFP.faces[0][1].idFace = 1;
        m_360scvpParam->paramViewPort.paramVideoFP.faces[0][1].rotFace = NO_TRANSFORM;
        m_360scvpParam->paramViewPort.paramVideoFP.faces[0][2].idFace = 2;
        m_360scvpParam->paramViewPort.paramVideoFP.faces[0][2].rotFace = NO_TRANSFORM;
        m_360scvpParam->paramViewPort.paramVideoFP.faces[1][0].idFace = 3;
        m_360scvpParam->paramViewPort.paramVideoFP.faces[1][0].rotFace = NO_TRANSFORM;
        m_360scvpParam->paramViewPort.paramVideoFP.faces[1][1].idFace = 4;
        m_360scvpParam->paramViewPort.paramVideoFP.faces[1][1].rotFace = NO_TRANSFORM;
        m_360scvpParam->paramViewPort.paramVideoFP.faces[1][2].idFace = 5;
        m_360scvpParam->paramViewPort.paramVideoFP.faces[1][2].rotFace = NO_TRANSFORM;
    }

    m_360scvpHandle = I360SCVP_Init(m_360scvpParam);
    if (!m_360scvpHandle)
    {
        OMAF_LOG(LOG_ERROR, "Failed to create 360SCVP handle !\n");
        return OMAF_ERROR_SCVP_INIT_FAILED;
    }

    for (float one_yaw = -180.0; one_yaw <= 180.0; )
    {
        for (float one_pitch = -90.0; one_pitch <= 90.0; )
        {
            int ret = SelectTilesInView(one_yaw, one_pitch, tileInRow, tileInCol);
            if (ret)
                return ret;

            one_pitch += PITCH_STEP;
        }

        one_yaw += YAW_STEP;
    }

    DELETE_MEMORY(m_360scvpParam);
    I360SCVP_unInit(m_360scvpHandle);
    m_360scvpHandle = NULL;

    return ERROR_NONE;
}

int32_t ExtractorTrackGenerator::FillDstRegionWisePacking(
    RegionWisePackingGenerator *rwpkGen,
    TileDef *tilesInViewport,
    RegionWisePacking *dstRwpk)
{
    if (!rwpkGen || !tilesInViewport || !dstRwpk)
        return OMAF_ERROR_NULL_PTR;

    dstRwpk->projPicWidth  = m_origResWidth;
    dstRwpk->projPicHeight = m_origResHeight;

    int32_t ret = rwpkGen->GenerateDstRwpk(tilesInViewport, dstRwpk);
    if (ret)
        return ret;

    m_packedPicWidth  = rwpkGen->GetPackedPicWidth();
    m_packedPicHeight = rwpkGen->GetPackedPicHeight();

    return ERROR_NONE;
}

int32_t ExtractorTrackGenerator::FillTilesMergeDirection(
    RegionWisePackingGenerator *rwpkGen,
    TileDef *tilesInViewport,
    TilesMergeDirectionInCol *tilesMergeDir)
{
    if (!rwpkGen || !tilesInViewport || !tilesMergeDir)
        return OMAF_ERROR_NULL_PTR;

    int32_t ret = rwpkGen->GenerateTilesMergeDirection(tilesInViewport, tilesMergeDir);
    if (ret)
        return ret;

    return ERROR_NONE;
}

int32_t ExtractorTrackGenerator::FillDstContentCoverage(
    uint16_t viewportIdx,
    ContentCoverage *dstCovi)
{
    CCDef *viewportCC = NULL;
    viewportCC = m_viewportCCInfo[viewportIdx];
    if (!viewportCC)
    {
        OMAF_LOG(LOG_ERROR, "There is no calculated CC info for the viewport !\n");
        return OMAF_ERROR_NULL_PTR;
    }

    if (m_projType == VCD::OMAF::ProjectionFormat::PF_ERP)
    {
        dstCovi->coverageShapeType = 1;
    }
    else
    {
        dstCovi->coverageShapeType = 0;
    }

    dstCovi->numRegions          = 1;
    dstCovi->viewIdcPresenceFlag = false;
    dstCovi->defaultViewIdc      = 0;

    dstCovi->sphereRegions = new SphereRegion[dstCovi->numRegions];
    if (!dstCovi->sphereRegions)
        return OMAF_ERROR_NULL_PTR;

    SphereRegion *sphereRegion    = &(dstCovi->sphereRegions[0]);
    memset_s(sphereRegion, sizeof(SphereRegion), 0);
    sphereRegion->viewIdc         = 0;
    sphereRegion->centreAzimuth   = viewportCC->centreAzimuth;
    sphereRegion->centreElevation = viewportCC->centreElevation;
    sphereRegion->centreTilt      = 0;
    sphereRegion->azimuthRange    = viewportCC->azimuthRange;
    sphereRegion->elevationRange  = viewportCC->elevationRange;
    sphereRegion->interpolate     = 0;

    return ERROR_NONE;
}

int32_t ExtractorTrackGenerator::CheckAndFillInitInfo()
{
    if (!m_initInfo)
        return OMAF_ERROR_NULL_PTR;

    uint8_t actualVideoNum = 0;
    uint8_t totalStreamNum = m_initInfo->bsNumVideo + m_initInfo->bsNumAudio;
    m_videoIdxInMedia = new uint8_t[totalStreamNum];
    if (!m_videoIdxInMedia)
        return OMAF_ERROR_NULL_PTR;

    memset_s(m_videoIdxInMedia, totalStreamNum * sizeof(uint8_t), 0);

    for (uint8_t streamIdx = 0; streamIdx < totalStreamNum; streamIdx++)
    {
        BSBuffer *bs = &(m_initInfo->bsBuffers[streamIdx]);
        if (bs->mediaType == VIDEOTYPE)
        {
            actualVideoNum++;
        }
    }

    if (actualVideoNum != m_initInfo->bsNumVideo)
        return OMAF_ERROR_VIDEO_NUM;

    std::set<uint64_t> bitRateRanking;

    std::map<uint8_t, MediaStream*>::iterator it;
    for (it = m_streams->begin(); it != m_streams->end(); it++)
    {
        MediaStream *stream = it->second;
        if (stream->GetMediaType() == VIDEOTYPE)
        {
            VideoStream *vs = (VideoStream*)stream;
            uint64_t bitRate = vs->GetBitRate();
            bitRateRanking.insert(bitRate);
        }
    }

    std::set<uint64_t>::reverse_iterator rateIter = bitRateRanking.rbegin();
    uint8_t vsIdx = 0;
    for ( ; rateIter != bitRateRanking.rend(); rateIter++)
    {
        uint64_t bitRate = *rateIter;
        for (it = m_streams->begin(); it != m_streams->end(); it++)
        {
            MediaStream *stream = it->second;
            if (stream->GetMediaType() == VIDEOTYPE)
            {
                VideoStream *vs = (VideoStream*)stream;
                uint64_t videoBitRate = vs->GetBitRate();
                if (videoBitRate == bitRate)
                {
                    m_videoIdxInMedia[vsIdx] = it->first; //rank video index from largest bitrate to smallest bitrate
                    break;
                }
            }
        }
        vsIdx++;
    }

    uint8_t mainVSId = m_videoIdxInMedia[0];
    it = m_streams->find(mainVSId);
    if (it == m_streams->end())
        return OMAF_ERROR_STREAM_NOT_FOUND;

    VideoStream *mainVS = (VideoStream*)(it->second);
    m_origResWidth = mainVS->GetSrcWidth();
    m_origResHeight = mainVS->GetSrcHeight();
    m_origTileInRow = mainVS->GetTileInRow();
    m_origTileInCol = mainVS->GetTileInCol();
    m_tilesInfo = mainVS->GetAllTilesInfo();
    m_origTileWidth = m_tilesInfo[0].tileWidth;
    m_origTileHeight = m_tilesInfo[0].tileHeight;
    m_projType = (VCD::OMAF::ProjectionFormat)(mainVS->GetProjType());

    (m_initInfo->viewportInfo)->inWidth    = m_origResWidth;
    (m_initInfo->viewportInfo)->inHeight   = m_origResHeight;
    (m_initInfo->viewportInfo)->tileInRow  = m_origTileInRow;
    (m_initInfo->viewportInfo)->tileInCol  = m_origTileInCol;
    (m_initInfo->viewportInfo)->outGeoType = 2; //viewport
    (m_initInfo->viewportInfo)->inGeoType  = mainVS->GetProjType();

    if ((m_initInfo->segmentationInfo)->extractorTracksPerSegThread == 0)
    {
        if ((m_origTileInRow * m_origTileInCol) % 4 == 0)
        {
            (m_initInfo->segmentationInfo)->extractorTracksPerSegThread = 4;
        }
        else if ((m_origTileInRow * m_origTileInCol) % 3 == 0)
        {
            (m_initInfo->segmentationInfo)->extractorTracksPerSegThread = 3;
        }
        else if ((m_origTileInRow * m_origTileInCol) % 2 == 0)
        {
            (m_initInfo->segmentationInfo)->extractorTracksPerSegThread = 2;
        }
        else
        {
            (m_initInfo->segmentationInfo)->extractorTracksPerSegThread = 1;
        }
    }

    return ERROR_NONE;
}

int32_t ExtractorTrackGenerator::Initialize()
{
    if (!m_initInfo)
        return OMAF_ERROR_NULL_PTR;

    m_fixedPackedPicRes = m_initInfo->fixedPackedPicRes;

    int32_t ret = CheckAndFillInitInfo();
    if (ret)
        return ret;

    std::map<uint8_t, MediaStream*>::iterator it;
    it = m_streams->find(m_videoIdxInMedia[0]); //high resolution video stream
    if (it == m_streams->end())
        return OMAF_ERROR_STREAM_NOT_FOUND;

    VideoStream *vs = (VideoStream*)(it->second);
    m_origVPSNalu   = vs->GetVPSNalu();
    m_origSPSNalu   = vs->GetSPSNalu();
    m_origPPSNalu   = vs->GetPPSNalu();

#ifdef _USE_TRACE_
    //trace
    if ((EGeometryType)((m_initInfo->viewportInfo)->inGeoType) == EGeometryType::E_SVIDEO_EQUIRECT)
    {
        const char *projType = "ERP";
        tracepoint(bandwidth_tp_provider, initial_viewport_info,
            (m_initInfo->viewportInfo)->viewportWidth,
            (m_initInfo->viewportInfo)->viewportHeight,
            (m_initInfo->viewportInfo)->viewportPitch,
            (m_initInfo->viewportInfo)->viewportYaw,
            (m_initInfo->viewportInfo)->horizontalFOVAngle,
            (m_initInfo->viewportInfo)->verticalFOVAngle,
            projType);
    }
    else if ((EGeometryType)((m_initInfo->viewportInfo)->inGeoType) == EGeometryType::E_SVIDEO_CUBEMAP)
    {
        const char *projType = "CubeMap";
        tracepoint(bandwidth_tp_provider, initial_viewport_info,
            (m_initInfo->viewportInfo)->viewportWidth,
            (m_initInfo->viewportInfo)->viewportHeight,
            (m_initInfo->viewportInfo)->viewportPitch,
            (m_initInfo->viewportInfo)->viewportYaw,
            (m_initInfo->viewportInfo)->horizontalFOVAngle,
            (m_initInfo->viewportInfo)->verticalFOVAngle,
            projType);
    }
#endif

    ret = CalculateViewportNum();
    if (ret)
        return ret;

    OMAF_LOG(LOG_INFO, "Total Viewport number is %d\n", m_viewportNum);

    std::set<uint16_t> allSelectedNums;
    std::map<uint16_t, std::map<uint16_t, TileDef*>>::iterator itSelection;
    for (itSelection = m_tilesSelection.begin(); itSelection != m_tilesSelection.end(); itSelection++)
    {
        uint16_t selectedNum = itSelection->first;
        allSelectedNums.insert(selectedNum);
    }
    std::set<uint16_t>::reverse_iterator numIter = allSelectedNums.rbegin();
    uint16_t maxSelectedNum = *numIter;
    OMAF_LOG(LOG_INFO, "Maxmum selected tiles number in viewport is %d\n", maxSelectedNum);

    for (itSelection = m_tilesSelection.begin(); itSelection != m_tilesSelection.end(); itSelection++)
    {
        uint16_t selectedNum = itSelection->first;

        RegionWisePackingGenerator *rwpkGen = new RegionWisePackingGenerator();
        if (!rwpkGen)
            return OMAF_ERROR_NULL_PTR;

        if (m_fixedPackedPicRes)
        {
            ret = rwpkGen->Initialize(
                 m_initInfo->pluginPath, m_initInfo->pluginName,
                 m_streams, m_videoIdxInMedia,
                 selectedNum, maxSelectedNum, logCallBack);
        }
        else
        {
            ret = rwpkGen->Initialize(
                 m_initInfo->pluginPath, m_initInfo->pluginName,
                 m_streams, m_videoIdxInMedia,
                 selectedNum, selectedNum, logCallBack);
        }

        if (ret)
        {
            DELETE_MEMORY(rwpkGen);
            return ret;
        }

        m_rwpkGenMap.insert(std::make_pair(selectedNum, rwpkGen));
    }

    return ERROR_NONE;
}

int32_t ExtractorTrackGenerator::GenerateExtractorTracks(
    std::map<uint8_t, ExtractorTrack*>& extractorTrackMap,
    std::map<uint8_t, MediaStream*> *streams)
{
    if (!streams)
        return OMAF_ERROR_NULL_PTR;

    int32_t ret = ERROR_NONE;

    std::list<PicResolution> picResolution;
    std::map<uint8_t, MediaStream*>::iterator itStr;
    uint8_t videoNum = m_initInfo->bsNumVideo;
    for (uint8_t vsIdx = 0; vsIdx < videoNum; vsIdx++)
    {
        itStr = m_streams->find(m_videoIdxInMedia[vsIdx]);
        if (itStr == m_streams->end())
            return OMAF_ERROR_STREAM_NOT_FOUND;

        VideoStream *vs = (VideoStream*)(itStr->second);

        PicResolution resolution = { vs->GetSrcWidth(), vs->GetSrcHeight() };
        picResolution.push_back(resolution);
    }

    std::map<uint16_t, std::map<uint16_t, TileDef*>>::iterator it;
    for (it = m_tilesSelection.begin(); it != m_tilesSelection.end(); it++)
    {
        uint16_t selectedNum = it->first;
        RegionWisePackingGenerator *rwpkGen = m_rwpkGenMap[selectedNum];
        if (!rwpkGen)
            return OMAF_ERROR_NULL_PTR;

        std::map<uint16_t, TileDef*> oneLayout = it->second;
        std::map<uint16_t, TileDef*>::iterator it1;
        for (it1 = oneLayout.begin(); it1 != oneLayout.end(); it1++)
        {
            uint16_t viewportIdx = it1->first;
            TileDef *tilesInView = it1->second;
            if (!tilesInView)
                return OMAF_ERROR_NULL_PTR;

            ExtractorTrack *extractorTrack = new ExtractorTrack(viewportIdx, streams, (m_initInfo->viewportInfo)->inGeoType);

            if (!extractorTrack)
            {
                std::map<uint8_t, ExtractorTrack*>::iterator itET = extractorTrackMap.begin();
                for ( ; itET != extractorTrackMap.end(); )
                {
                    ExtractorTrack *extractorTrack1 = itET->second;
                    DELETE_MEMORY(extractorTrack1);
                    extractorTrackMap.erase(itET++);
                }
                extractorTrackMap.clear();
                return OMAF_ERROR_NULL_PTR;
            }

            int32_t retInit = extractorTrack->Initialize();
            if (retInit)
            {
                OMAF_LOG(LOG_ERROR, "Failed to initialize extractor track !\n");

                std::map<uint8_t, ExtractorTrack*>::iterator itET = extractorTrackMap.begin();
                for ( ; itET != extractorTrackMap.end(); )
                {
                    ExtractorTrack *extractorTrack1 = itET->second;
                    DELETE_MEMORY(extractorTrack1);
                    extractorTrackMap.erase(itET++);
                }
                extractorTrackMap.clear();
                DELETE_MEMORY(extractorTrack);
                return retInit;
            }

            ret = rwpkGen->GenerateMergedTilesArrange(tilesInView);
            if (ret)
            {
                std::map<uint8_t, ExtractorTrack*>::iterator itET = extractorTrackMap.begin();
                for ( ; itET != extractorTrackMap.end(); )
                {
                    ExtractorTrack *extractorTrack1 = itET->second;
                    DELETE_MEMORY(extractorTrack1);
                    extractorTrackMap.erase(itET++);
                }
                extractorTrackMap.clear();
                DELETE_MEMORY(extractorTrack);
                return ret;
            }

            ret = FillDstRegionWisePacking(rwpkGen, tilesInView, extractorTrack->GetRwpk());
            if (ret)
            {
                std::map<uint8_t, ExtractorTrack*>::iterator itET = extractorTrackMap.begin();
                for ( ; itET != extractorTrackMap.end(); )
                {
                    ExtractorTrack *extractorTrack1 = itET->second;
                    DELETE_MEMORY(extractorTrack1);
                    extractorTrackMap.erase(itET++);
                }
                extractorTrackMap.clear();
                DELETE_MEMORY(extractorTrack);
                return ret;
            }

            ret = FillTilesMergeDirection(rwpkGen, tilesInView, extractorTrack->GetTilesMergeDir());
            if (ret)
            {
                std::map<uint8_t, ExtractorTrack*>::iterator itET = extractorTrackMap.begin();
                for ( ; itET != extractorTrackMap.end(); )
                {
                    ExtractorTrack *extractorTrack1 = itET->second;
                    DELETE_MEMORY(extractorTrack1);
                    extractorTrackMap.erase(itET++);
                }
                extractorTrackMap.clear();
                DELETE_MEMORY(extractorTrack);
                return ret;
            }

            ret = FillDstContentCoverage(viewportIdx, extractorTrack->GetCovi());
            if (ret)
            {
                std::map<uint8_t, ExtractorTrack*>::iterator itET = extractorTrackMap.begin();
                for ( ; itET != extractorTrackMap.end(); )
                {
                    ExtractorTrack *extractorTrack1 = itET->second;
                    DELETE_MEMORY(extractorTrack1);
                    extractorTrackMap.erase(itET++);
                }
                extractorTrackMap.clear();
                DELETE_MEMORY(extractorTrack);
                return ret;
            }

            ret = GenerateNewSPS();
            if (ret)
            {
                std::map<uint8_t, ExtractorTrack*>::iterator itET = extractorTrackMap.begin();
                for ( ; itET != extractorTrackMap.end(); )
                {
                    ExtractorTrack *extractorTrack1 = itET->second;
                    DELETE_MEMORY(extractorTrack1);
                    extractorTrackMap.erase(itET++);
                }
                extractorTrackMap.clear();
                DELETE_MEMORY(extractorTrack);
                return ret;
            }

            ret = GenerateNewPPS(rwpkGen);
            if (ret)
            {
                std::map<uint8_t, ExtractorTrack*>::iterator itET = extractorTrackMap.begin();
                for ( ; itET != extractorTrackMap.end(); )
                {
                    ExtractorTrack *extractorTrack1 = itET->second;
                    DELETE_MEMORY(extractorTrack1);
                    extractorTrackMap.erase(itET++);
                }
                extractorTrackMap.clear();
                DELETE_MEMORY(extractorTrack);
                return ret;
            }

            extractorTrack->SetPackedPicWidth(m_packedPicWidth);
            extractorTrack->SetPackedPicHeight(m_packedPicHeight);
            extractorTrack->SetNalu(m_origVPSNalu, extractorTrack->GetVPS());
            extractorTrack->SetNalu(m_newSPSNalu, extractorTrack->GetSPS());
            extractorTrack->SetNalu(m_newPPSNalu, extractorTrack->GetPPS());

            std::list<PicResolution>* picResList = extractorTrack->GetPicRes();
            std::list<PicResolution>::iterator itRes;
            for (itRes = picResolution.begin(); itRes != picResolution.end(); itRes++)
            {
                PicResolution picRes = *itRes;
                picResList->push_back(picRes);
            }

            extractorTrackMap.insert(std::make_pair(viewportIdx, std::move(extractorTrack)));
        }
    }

    return ERROR_NONE;
}

int32_t ExtractorTrackGenerator::GenerateNewSPS()
{
    if (!m_packedPicWidth || !m_packedPicHeight || !m_origSPSNalu)
        return OMAF_ERROR_BAD_PARAM;

    if (!(m_origSPSNalu->data) || !(m_origSPSNalu->dataSize))
        return OMAF_ERROR_INVALID_SPS;

    if (!m_360scvpParam || !m_360scvpHandle)
    {
        std::map<uint8_t, MediaStream*>::iterator it;
        it = m_streams->find(m_videoIdxInMedia[0]);
        if (it == m_streams->end())
            return OMAF_ERROR_STREAM_NOT_FOUND;

        VideoStream *vs = (VideoStream*)(it->second);
        m_360scvpHandle = vs->Get360SCVPHandle();
        m_360scvpParam  = vs->Get360SCVPParam();

        if (!m_360scvpParam || !m_360scvpHandle)
            return OMAF_ERROR_NULL_PTR;
    }

    if (m_newSPSNalu)
    {
        DELETE_ARRAY(m_newSPSNalu->data);
        DELETE_MEMORY(m_newSPSNalu);
    }

    m_newSPSNalu = new Nalu;
    if (!m_newSPSNalu)
        return OMAF_ERROR_NULL_PTR;

    m_newSPSNalu->data = new uint8_t[1024];//include start codes
    if (!m_newSPSNalu->data)
        return OMAF_ERROR_NULL_PTR;

    m_360scvpParam->pInputBitstream   = m_origSPSNalu->data;
    m_360scvpParam->inputBitstreamLen = m_origSPSNalu->dataSize;
    m_360scvpParam->destWidth         = m_packedPicWidth;
    m_360scvpParam->destHeight        = m_packedPicHeight;
    m_360scvpParam->pOutputBitstream  = m_newSPSNalu->data;

    int32_t ret = I360SCVP_GenerateSPS(m_360scvpParam, m_360scvpHandle);
    if (ret)
        return OMAF_ERROR_SCVP_OPERATION_FAILED;

    m_newSPSNalu->dataSize       = m_360scvpParam->outputBitstreamLen;
    m_newSPSNalu->startCodesSize = HEVC_STARTCODES_LEN;
    m_newSPSNalu->naluType       = HEVC_SPS_NALU_TYPE;

    return ERROR_NONE;
}

int32_t ExtractorTrackGenerator::GenerateNewPPS(RegionWisePackingGenerator *rwpkGen)
{
    if (!rwpkGen)
        return OMAF_ERROR_NULL_PTR;

    TileArrangement *tileArray = rwpkGen->GetMergedTilesArrange();
    if (!tileArray)
        return OMAF_ERROR_NULL_PTR;

    if (!m_360scvpParam || !m_360scvpHandle)
    {
        std::map<uint8_t, MediaStream*>::iterator it;
        it = m_streams->find(m_videoIdxInMedia[0]);
        if (it == m_streams->end())
            return OMAF_ERROR_STREAM_NOT_FOUND;

        VideoStream *vs = (VideoStream*)(it->second);
        m_360scvpHandle = vs->Get360SCVPHandle();
        m_360scvpParam  = vs->Get360SCVPParam();

        if (!m_360scvpParam || !m_360scvpHandle)
            return OMAF_ERROR_NULL_PTR;
    }

    if (m_newPPSNalu)
    {
        DELETE_ARRAY(m_newPPSNalu->data);
        DELETE_MEMORY(m_newPPSNalu);
    }

    m_newPPSNalu = new Nalu;
    if (!m_newPPSNalu)
        return OMAF_ERROR_NULL_PTR;

    m_newPPSNalu->data     = new uint8_t[1024];//include start codes
    if (!m_newPPSNalu->data)
        return OMAF_ERROR_NULL_PTR;

    m_360scvpParam->pInputBitstream   = m_origPPSNalu->data; //includes start codes
    m_360scvpParam->inputBitstreamLen = m_origPPSNalu->dataSize;

    m_360scvpParam->pOutputBitstream  = m_newPPSNalu->data;

    int32_t ret = I360SCVP_GeneratePPS(m_360scvpParam, tileArray, m_360scvpHandle);
    if (ret)
        return OMAF_ERROR_SCVP_OPERATION_FAILED;

    m_newPPSNalu->dataSize = m_360scvpParam->outputBitstreamLen;
    m_newPPSNalu->startCodesSize = HEVC_STARTCODES_LEN;
    m_newPPSNalu->naluType = HEVC_PPS_NALU_TYPE;

    return ERROR_NONE;
}

VCD_NS_END
