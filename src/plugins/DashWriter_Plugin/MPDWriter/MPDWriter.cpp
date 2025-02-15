/*
 * Copyright (c) 2021, Intel Corporation
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
//! \file:   MPDWriter.cpp
//! \brief:  DASH MPD writer plugin implementation for omnidirectional media
//!
//! Created on Dec. 1, 2021, 6:04 AM
//!

#include <unistd.h>
#include <sys/stat.h>
#include <sys/timeb.h>
#include <time.h>
#include "MPDWriter.h"
#include "OmafPackingLog.h"
#include "error.h"
#include "VideoStreamPluginAPI.h"

MPDWriter::MPDWriter()
{
    m_streamASCtx = NULL;
    m_extractorASCtx = NULL;
    m_segInfo = NULL;
    m_projType = VCD::OMAF::ProjectionFormat::PF_ERP;
    m_miniUpdatePeriod = 0;
    memset_s(m_availableStartTime, 1024, 0);
    m_publishTime = NULL;
    m_presentationDur = NULL;
    memset_s(m_mpdFileName, 1024, 0);
    m_timeScale = 0;
    m_xmlDoc = NULL;
    m_frameRate.num = 0;
    m_frameRate.den = 0;
    m_vsNum = 0;
    m_cmafEnabled = false;
    m_currSegNum = 0;
}

MPDWriter::MPDWriter(
    std::map<MediaStream*, VCD::MP4::MPDAdaptationSetCtx*> *streamsASCtxs,
    std::map<uint32_t, VCD::MP4::MPDAdaptationSetCtx*> *extractorASCtxs,
    SegmentationInfo *segInfo,
    VCD::OMAF::ProjectionFormat projType,
    Rational frameRate,
    uint8_t  videoNum,
    bool     cmafEnabled)
{
    m_streamASCtx = streamsASCtxs;
    m_extractorASCtx = extractorASCtxs;
    m_segInfo = segInfo;
    m_projType = projType;
    m_miniUpdatePeriod = 0;
    memset_s(m_availableStartTime, 1024, 0);
    memset_s(m_mpdFileName, 1024, 0);
    m_publishTime = NULL;
    m_presentationDur = NULL;
    m_frameRate = frameRate;
    m_timeScale = 0;
    m_xmlDoc = NULL;
    m_vsNum = videoNum;
    m_cmafEnabled = cmafEnabled;
    m_currSegNum = 0;
}

MPDWriter::MPDWriter(const MPDWriter& src)
{
    m_streamASCtx = std::move(src.m_streamASCtx);
    m_extractorASCtx = std::move(src.m_extractorASCtx);
    m_segInfo = std::move(src.m_segInfo);
    m_projType = src.m_projType;
    m_miniUpdatePeriod = src.m_miniUpdatePeriod;
    memset_s(m_availableStartTime, 1024, 0);
    memset_s(m_mpdFileName, 1024, 0);
    m_publishTime = std::move(src.m_publishTime);
    m_presentationDur = std::move(src.m_presentationDur);
    m_timeScale = src.m_timeScale;
    m_xmlDoc = std::move(src.m_xmlDoc);
    m_frameRate.num = src.m_frameRate.num;
    m_frameRate.den = src.m_frameRate.den;
    m_vsNum         = src.m_vsNum;
    m_cmafEnabled   = src.m_cmafEnabled;
    m_currSegNum    = src.m_currSegNum;
}

MPDWriter& MPDWriter::operator=(MPDWriter&& other)
{
    m_streamASCtx = std::move(other.m_streamASCtx);
    m_extractorASCtx = std::move(other.m_extractorASCtx);
    m_segInfo = std::move(other.m_segInfo);
    m_projType = other.m_projType;
    m_miniUpdatePeriod = other.m_miniUpdatePeriod;
    memset_s(m_availableStartTime, 1024, 0);
    memset_s(m_mpdFileName, 1024, 0);
    m_publishTime = std::move(other.m_publishTime);
    m_presentationDur = std::move(other.m_presentationDur);
    m_timeScale = other.m_timeScale;
    m_xmlDoc = std::move(other.m_xmlDoc);
    m_frameRate.num = other.m_frameRate.num;
    m_frameRate.den = other.m_frameRate.den;
    m_vsNum         = other.m_vsNum;
    m_cmafEnabled   = other.m_cmafEnabled;
    m_currSegNum    = other.m_currSegNum;

    return *this;
}

MPDWriter::~MPDWriter()
{
    DELETE_ARRAY(m_publishTime);
    DELETE_ARRAY(m_presentationDur);
    DELETE_MEMORY(m_xmlDoc);
}

int32_t MPDWriter::Initialize()
{
    if (!m_segInfo)
        return OMAF_ERROR_NULL_PTR;

    int32_t modeU = 7;
    int32_t modeG = 7;
    int32_t modeO = 7;
    int32_t modeFile = modeU * 64 + modeG * 8 + modeO;
    char buf[PATH_MAX] = { 0 };
    char *res = realpath(&(m_segInfo->dirName[0]), buf);
    if (res)
    {
        OMAF_LOG(LOG_INFO, "Folder %s has already existed\n", m_segInfo->dirName);
    }
    else
    {
        if (mkdir(&(m_segInfo->dirName[0]), modeFile) != 0)
        {
            OMAF_LOG(LOG_ERROR, "Failed to create folder %s\n", m_segInfo->dirName);
            return OMAF_ERROR_CREATE_FOLDER_FAILED;
        }
    }

    snprintf(m_mpdFileName, sizeof(m_mpdFileName), "%s%s.mpd", m_segInfo->dirName, m_segInfo->outName);

    if (m_segInfo->windowSize > 0)
    {
        m_miniUpdatePeriod = m_segInfo->segDuration * m_segInfo->windowSize;
    }
    else
    {
        m_miniUpdatePeriod = m_segInfo->segDuration; //uint is second
    }


    uint64_t fps1000 = (uint64_t) ((double)(m_frameRate.num / m_frameRate.den) * 1000 + 0.5);
    if (fps1000 == 29970)
        m_timeScale = 30000;
    else if (fps1000 == 23976)
        m_timeScale = 24000;
    else if (fps1000 == 59940)
        m_timeScale = 60000;
    else
        m_timeScale = fps1000;

    m_xmlDoc = new XMLDocument;
    if (!m_xmlDoc)
        return OMAF_ERROR_CREATE_XMLFILE_FAILED;

    return ERROR_NONE;
}

int32_t MPDWriter::WriteTileTrackAS(XMLElement *periodEle, VCD::MP4::MPDAdaptationSetCtx *pTrackASCtx)
{
    VCD::MP4::MPDAdaptationSetCtx *trackASCtx = pTrackASCtx;

    char string[1024];
    memset_s(string, 1024, 0);

    XMLElement *asEle = m_xmlDoc->NewElement(ADAPTATIONSET);
    asEle->SetAttribute(INDEX, trackASCtx->trackIdx.GetIndex());
    asEle->SetAttribute(MIMETYPE, MIMETYPE_VALUE); //?
    asEle->SetAttribute(CODECS, CODECS_VALUE);
    asEle->SetAttribute(MAXWIDTH, trackASCtx->videoInfo->tileWidth);
    asEle->SetAttribute(MAXHEIGHT, trackASCtx->videoInfo->tileHeight);
    snprintf(string, 1024, "%ld/%ld", m_frameRate.num, m_frameRate.den);
    asEle->SetAttribute(MAXFRAMERATE, string);
    asEle->SetAttribute(SEGMENTALIGNMENT, 1);
    asEle->SetAttribute(SUBSEGMENTALIGNMENT, 1);
    periodEle->InsertEndChild(asEle);

    XMLElement *viewportEle = m_xmlDoc->NewElement(VIEWPORT);
    viewportEle->SetAttribute(SCHEMEIDURI, SCHEMEIDURI_VIEWPORT);
    viewportEle->SetAttribute(COMMON_VALUE, "vpl");
    asEle->InsertEndChild(viewportEle);

    XMLElement *supplementalEle = m_xmlDoc->NewElement(SUPPLEMENTALPROPERTY);
    supplementalEle->SetAttribute(SCHEMEIDURI, SCHEMEIDURI_SRD);
    memset_s(string, 1024, 0);
    if ((m_projType == VCD::OMAF::ProjectionFormat::PF_ERP) ||
        (m_projType == VCD::OMAF::ProjectionFormat::PF_PLANAR))
    {
        snprintf(string, 1024, "1,%d,%d,%d,%d", trackASCtx->videoInfo->tileHPos, trackASCtx->videoInfo->tileVPos, trackASCtx->videoInfo->tileWidth, trackASCtx->videoInfo->tileHeight);
    }
    else if (m_projType == VCD::OMAF::ProjectionFormat::PF_CUBEMAP)
    {
        snprintf(string, 1024, "1,%d,%d,%d,%d", trackASCtx->videoInfo->tileCubemapHPos, trackASCtx->videoInfo->tileCubemapVPos, trackASCtx->videoInfo->tileWidth, trackASCtx->videoInfo->tileHeight);
    }

    supplementalEle->SetAttribute(COMMON_VALUE, string);
    asEle->InsertEndChild(supplementalEle);

    XMLElement *essentialEle1 = m_xmlDoc->NewElement(ESSENTIALPROPERTY);
    essentialEle1->SetAttribute(SCHEMEIDURI, SCHEMEIDURI_RWPK);
    essentialEle1->SetAttribute(OMAF_PACKINGTYPE, 0);
    asEle->InsertEndChild(essentialEle1);

    if (m_cmafEnabled && m_segInfo->isLive)
    {
        XMLElement *prftEle = m_xmlDoc->NewElement(PRODUCERREFERENCETIME);
        prftEle->SetAttribute(INDEX, "0");
        prftEle->SetAttribute(INBAND, "true");
        prftEle->SetAttribute(TIMETYPE, "encoder");

        uint32_t sec;
        time_t gTime;
        struct tm *t;
        struct timeval now;
        struct timeb timeBuffer;
        ftime(&timeBuffer);
        now.tv_sec = (long)(timeBuffer.time);
        now.tv_usec = timeBuffer.millitm * 1000;
        sec = (uint32_t)(now.tv_sec) + NTP_SEC_1900_TO_1970;

        gTime = sec - NTP_SEC_1900_TO_1970;
        t = gmtime(&gTime);
        if (!t)
            return OMAF_ERROR_INVALID_TIME;

        char currTime[1024];
        memset_s(currTime, 1024, 0);
        snprintf(currTime, 1024, "%d-%d-%dT%d:%d:%dZ", 1900 + t->tm_year,
                t->tm_mon + 1, t->tm_mday, t->tm_hour, t->tm_min, t->tm_sec);

        prftEle->SetAttribute(WALLCLOCKTIME, currTime);
        prftEle->SetAttribute(PRESENTATIONTIME, "0");
        asEle->InsertEndChild(prftEle);

        XMLElement *utcEle = m_xmlDoc->NewElement(UTCTIMING);
        utcEle->SetAttribute(SCHEMEIDURI, SCHEMEIDURI_UTCTIMING);
        utcEle->SetAttribute(COMMON_VALUE, UTCTIMING_VALUE);
        prftEle->InsertEndChild(utcEle);
    }

    XMLElement *representationEle = m_xmlDoc->NewElement(REPRESENTATION);
    memset_s(string, 1024, 0);
    snprintf(string, 1024, "%s_track%d", m_segInfo->outName, trackASCtx->trackIdx.GetIndex());
    representationEle->SetAttribute(INDEX, string);
    representationEle->SetAttribute(QUALITYRANKING, trackASCtx->qualityRanking);
    representationEle->SetAttribute(BANDWIDTH, trackASCtx->codedMeta.bitrate.avgBitrate);
    representationEle->SetAttribute(WIDTH, trackASCtx->videoInfo->tileWidth);
    representationEle->SetAttribute(HEIGHT, trackASCtx->videoInfo->tileHeight);
    snprintf(string, 1024, "%ld/%ld", m_frameRate.num, m_frameRate.den);
    representationEle->SetAttribute(FRAMERATE, string);
    representationEle->SetAttribute(SAR, "1:1");
    representationEle->SetAttribute(STARTWITHSAP, 1);
    asEle->InsertEndChild(representationEle);

    if (m_cmafEnabled)
    {
        memset_s(string, 1024, 0);
        snprintf(string, 1024, "%ld", m_segInfo->chunkDuration);
        XMLElement *resyncEle = m_xmlDoc->NewElement(RESYNC);
        resyncEle->SetAttribute(RESYNCTYPE, "0");
        resyncEle->SetAttribute(RESYNCDT, string);
        representationEle->InsertEndChild(resyncEle);
    }

    memset_s(string, 1024, 0);
    snprintf(string, 1024, "%s_track%d.$Number$.mp4", m_segInfo->outName, trackASCtx->trackIdx.GetIndex());
    XMLElement *sgtTpeEle = m_xmlDoc->NewElement(SEGMENTTEMPLATE);
    sgtTpeEle->SetAttribute(MEDIA, string);
    memset_s(string, 1024, 0);
    snprintf(string, 1024, "%s_track%d.init.mp4", m_segInfo->outName, trackASCtx->trackIdx.GetIndex());
    sgtTpeEle->SetAttribute(INITIALIZATION, string);
    sgtTpeEle->SetAttribute(DURATION, m_segInfo->segDuration * m_timeScale);
    if (!m_cmafEnabled || !(m_segInfo->isLive))
    {
        sgtTpeEle->SetAttribute(STARTNUMBER, 1);
    }
    else
    {
        int32_t currSegNum = (int32_t)m_currSegNum;//(int32_t)(trackASCtx->dashSegmenter->GetSegmentsNum());
        //OMAF_LOG(LOG_INFO, "Write start number %d to MPD \n", currSegNum);
        sgtTpeEle->SetAttribute(STARTNUMBER, currSegNum);
    }

    sgtTpeEle->SetAttribute(TIMESCALE, m_timeScale);

    if (m_cmafEnabled)
    {
        memset_s(string, 1024, 0);
        snprintf(string, 1024, "%f", ((float)(m_segInfo->segDuration) / 2));
        sgtTpeEle->SetAttribute(AVAILABILITYTIMEOFFSET, string);
        sgtTpeEle->SetAttribute(AVAILABILITYTIMECOMPLETE, "false");
    }
    representationEle->InsertEndChild(sgtTpeEle);

    return ERROR_NONE;
}

int32_t MPDWriter::WriteExtractorTrackAS(XMLElement *periodEle, VCD::MP4::MPDAdaptationSetCtx *pTrackASCtx)
{
    VCD::MP4::MPDAdaptationSetCtx *trackASCtx = pTrackASCtx;

    char string[1024];
    memset_s(string, 1024, 0);

    XMLElement *asEle = m_xmlDoc->NewElement(ADAPTATIONSET);
    asEle->SetAttribute(INDEX, trackASCtx->trackIdx.GetIndex());
    asEle->SetAttribute(MIMETYPE, MIMETYPE_VALUE); //?
    asEle->SetAttribute(CODECS, CODECS_VALUE_EXTRACTORTRACK);
    asEle->SetAttribute(MAXWIDTH, trackASCtx->codedMeta.width);
    asEle->SetAttribute(MAXHEIGHT, trackASCtx->codedMeta.height);
    snprintf(string, 1024, "%ld/%ld", m_frameRate.num, m_frameRate.den);
    asEle->SetAttribute(MAXFRAMERATE, string);
    asEle->SetAttribute(SEGMENTALIGNMENT, 1);
    asEle->SetAttribute(SUBSEGMENTALIGNMENT, 1);
    periodEle->InsertEndChild(asEle);

    XMLElement *viewportEle = m_xmlDoc->NewElement(VIEWPORT);
    viewportEle->SetAttribute(SCHEMEIDURI, SCHEMEIDURI_VIEWPORT);
    viewportEle->SetAttribute(COMMON_VALUE, "vpl");
    asEle->InsertEndChild(viewportEle);

    XMLElement *essentialEle = m_xmlDoc->NewElement(ESSENTIALPROPERTY);
    essentialEle->SetAttribute(SCHEMEIDURI, SCHEMEIDURI_RWPK);
    essentialEle->SetAttribute(OMAF_PACKINGTYPE, 0);
    asEle->InsertEndChild(essentialEle);

    if (m_cmafEnabled && m_segInfo->isLive)
    {
        XMLElement *prftEle = m_xmlDoc->NewElement(PRODUCERREFERENCETIME);
        prftEle->SetAttribute(INDEX, "0");
        prftEle->SetAttribute(INBAND, "true");
        prftEle->SetAttribute(TIMETYPE, "encoder");

        uint32_t sec;
        time_t gTime;
        struct tm *t;
        struct timeval now;
        struct timeb timeBuffer;
        ftime(&timeBuffer);
        now.tv_sec = (long)(timeBuffer.time);
        now.tv_usec = timeBuffer.millitm * 1000;
        sec = (uint32_t)(now.tv_sec) + NTP_SEC_1900_TO_1970;

        gTime = sec - NTP_SEC_1900_TO_1970;
        t = gmtime(&gTime);
        if (!t)
            return OMAF_ERROR_INVALID_TIME;

        char currTime[1024];
        memset_s(currTime, 1024, 0);
        snprintf(currTime, 1024, "%d-%d-%dT%d:%d:%dZ", 1900 + t->tm_year,
                t->tm_mon + 1, t->tm_mday, t->tm_hour, t->tm_min, t->tm_sec);

        prftEle->SetAttribute(WALLCLOCKTIME, currTime);
        prftEle->SetAttribute(PRESENTATIONTIME, "0");
        asEle->InsertEndChild(prftEle);

        XMLElement *utcEle = m_xmlDoc->NewElement(UTCTIMING);
        utcEle->SetAttribute(SCHEMEIDURI, SCHEMEIDURI_UTCTIMING);
        utcEle->SetAttribute(COMMON_VALUE, UTCTIMING_VALUE);
        prftEle->InsertEndChild(utcEle);
    }

    XMLElement *supplementalEle = m_xmlDoc->NewElement(SUPPLEMENTALPROPERTY);
    supplementalEle->SetAttribute(SCHEMEIDURI, SCHEMEIDURI_SRQR);
    asEle->InsertEndChild(supplementalEle);

    XMLElement *shpQualityEle = m_xmlDoc->NewElement(OMAF_SPHREGION_QUALITY);
    shpQualityEle->SetAttribute(SHAPE_TYPE, trackASCtx->codedMeta.qualityRankCoverage.get().shapeType);
    shpQualityEle->SetAttribute(REMAINING_AREA_FLAG, trackASCtx->codedMeta.qualityRankCoverage.get().remainingArea);
    shpQualityEle->SetAttribute(QUALITY_RANKING_LOCAL_FLAG, false);
    shpQualityEle->SetAttribute(QUALITY_TYPE, trackASCtx->codedMeta.qualityRankCoverage.get().qualityType);
    //shpQualityEle->SetAttribute(DEFAULT_VIEW_IDC, 0);
    supplementalEle->InsertEndChild(shpQualityEle);

    std::vector<VCD::MP4::QualityInfo>::iterator it;
    for (it = trackASCtx->codedMeta.qualityRankCoverage.get().qualityInfo.begin();
        it != trackASCtx->codedMeta.qualityRankCoverage.get().qualityInfo.end();
        it++)
    {
        VCD::MP4::QualityInfo oneQuality = *it;

        XMLElement *qualityEle = m_xmlDoc->NewElement(OMAF_QUALITY_INFO);
        qualityEle->SetAttribute(QUALITY_RANKING, oneQuality.qualityRank);
        qualityEle->SetAttribute(ORIGWIDTH, oneQuality.origWidth);
        qualityEle->SetAttribute(ORIGHEIGHT, oneQuality.origHeight);
        qualityEle->SetAttribute(CENTRE_AZIMUTH, oneQuality.sphere.get().cAzimuth);
        qualityEle->SetAttribute(CENTRE_ELEVATION, oneQuality.sphere.get().cElevation);
        qualityEle->SetAttribute(CENTRE_TILT, oneQuality.sphere.get().cTilt);
        qualityEle->SetAttribute(AZIMUTH_RANGE, oneQuality.sphere.get().rAzimuth);
        qualityEle->SetAttribute(ELEVATION_RANGE, oneQuality.sphere.get().rElevation);
        shpQualityEle->InsertEndChild(qualityEle);
    }

    memset_s(string, 1024, 0);
    snprintf(string, 1024, "ext%d,%d ", trackASCtx->trackIdx.GetIndex(), trackASCtx->trackIdx.GetIndex());
    std::list<VCD::MP4::TrackId>::iterator itRefTrack;
    for (itRefTrack = trackASCtx->refTrackIdxs.begin();
        itRefTrack != trackASCtx->refTrackIdxs.end();
        itRefTrack++)
    {
        char string1[16];
        memset_s(string1, 16, 0);
        snprintf(string1, 16, "%d ", (*itRefTrack).GetIndex());

        strncat(string, string1, 16);
    }

    XMLElement *supplementalEle1 = m_xmlDoc->NewElement(SUPPLEMENTALPROPERTY);
    supplementalEle1->SetAttribute(SCHEMEIDURI, SCHEMEIDURI_PRESELECTION);
    supplementalEle1->SetAttribute(COMMON_VALUE, string);
    asEle->InsertEndChild(supplementalEle1);

    XMLElement *representationEle = m_xmlDoc->NewElement(REPRESENTATION);
    memset_s(string, 1024, 0);
    snprintf(string, 1024, "%s_track%d", m_segInfo->outName, trackASCtx->trackIdx.GetIndex());
    representationEle->SetAttribute(INDEX, string);
    representationEle->SetAttribute(WIDTH, trackASCtx->codedMeta.width);
    representationEle->SetAttribute(HEIGHT, trackASCtx->codedMeta.height);
    memset_s(string, 1024, 0);
    snprintf(string, 1024, "%ld/%ld", m_frameRate.num, m_frameRate.den);
    representationEle->SetAttribute(FRAMERATE, string);
    asEle->InsertEndChild(representationEle);

    if (m_cmafEnabled)
    {
        memset_s(string, 1024, 0);
        snprintf(string, 1024, "%ld", m_segInfo->chunkDuration);
        XMLElement *resyncEle = m_xmlDoc->NewElement(RESYNC);
        resyncEle->SetAttribute(RESYNCTYPE, "0");
        resyncEle->SetAttribute(RESYNCDT, string);
        representationEle->InsertEndChild(resyncEle);
    }

    XMLElement *sgtTpeEle = m_xmlDoc->NewElement(SEGMENTTEMPLATE);
    memset_s(string, 1024, 0);
    snprintf(string, 1024, "%s_track%d.$Number$.mp4", m_segInfo->outName, trackASCtx->trackIdx.GetIndex());
    sgtTpeEle->SetAttribute(MEDIA, string);
    memset_s(string, 1024, 0);
    snprintf(string, 1024, "%s_track%d.init.mp4", m_segInfo->outName, trackASCtx->trackIdx.GetIndex());
    sgtTpeEle->SetAttribute(INITIALIZATION, string);
    sgtTpeEle->SetAttribute(DURATION, m_segInfo->segDuration * m_timeScale);
    if (!m_cmafEnabled || !(m_segInfo->isLive))
    {
        sgtTpeEle->SetAttribute(STARTNUMBER, 1);
    }
    else
    {
        sgtTpeEle->SetAttribute(STARTNUMBER, (int32_t)m_currSegNum);
    }
    sgtTpeEle->SetAttribute(TIMESCALE, m_timeScale);

    if (m_cmafEnabled)
    {
        memset_s(string, 1024, 0);
        snprintf(string, 1024, "%f", ((float)(m_segInfo->segDuration) / 2));
        sgtTpeEle->SetAttribute(AVAILABILITYTIMEOFFSET, string);
        sgtTpeEle->SetAttribute(AVAILABILITYTIMECOMPLETE, "false");
    }
    representationEle->InsertEndChild(sgtTpeEle);

    return ERROR_NONE;
}

int32_t MPDWriter::WriteAudioTrackAS(XMLElement *periodEle, VCD::MP4::MPDAdaptationSetCtx *pTrackASCtx)
{
    VCD::MP4::MPDAdaptationSetCtx *trackASCtx = pTrackASCtx;

    char string[1024];
    memset_s(string, 1024, 0);

    XMLElement *asEle = m_xmlDoc->NewElement(ADAPTATIONSET);
    asEle->SetAttribute(INDEX, trackASCtx->trackIdx.GetIndex());
    asEle->SetAttribute(MIMETYPE, MIMETYPE_AUDIO);
    asEle->SetAttribute(CODECS, CODECS_AUDIO);
    asEle->SetAttribute(AUDIOSAMPLINGRATE, trackASCtx->codedMeta.samplingFreq);

    asEle->SetAttribute(SEGMENTALIGNMENT, 1);
    asEle->SetAttribute(SUBSEGMENTALIGNMENT, 1);
    periodEle->InsertEndChild(asEle);

    XMLElement *representationEle = m_xmlDoc->NewElement(REPRESENTATION);
    memset_s(string, 1024, 0);
    snprintf(string, 1024, "%s_track%d", m_segInfo->outName, trackASCtx->trackIdx.GetIndex());
    representationEle->SetAttribute(INDEX, string);
    representationEle->SetAttribute(BANDWIDTH, trackASCtx->codedMeta.bitrate.avgBitrate);
    representationEle->SetAttribute(AUDIOSAMPLINGRATE, trackASCtx->codedMeta.samplingFreq);
    representationEle->SetAttribute(STARTWITHSAP, 1);
    asEle->InsertEndChild(representationEle);

    XMLElement *audioChlCfgEle = m_xmlDoc->NewElement(AUDIOCHANNELCONFIGURATION);
    audioChlCfgEle->SetAttribute(SCHEMEIDURI, SCHEMEIDURI_AUDIO);
    audioChlCfgEle->SetAttribute(COMMON_VALUE, trackASCtx->codedMeta.channelCfg);
    representationEle->InsertEndChild(audioChlCfgEle);

    memset_s(string, 1024, 0);
    snprintf(string, 1024, "%s_track%d.$Number$.mp4", m_segInfo->outName, trackASCtx->trackIdx.GetIndex());
    XMLElement *sgtTpeEle = m_xmlDoc->NewElement(SEGMENTTEMPLATE);
    sgtTpeEle->SetAttribute(MEDIA, string);
    memset_s(string, 1024, 0);
    snprintf(string, 1024, "%s_track%d.init.mp4", m_segInfo->outName, trackASCtx->trackIdx.GetIndex());
    sgtTpeEle->SetAttribute(INITIALIZATION, string);
    sgtTpeEle->SetAttribute(DURATION, m_segInfo->segDuration * m_timeScale);
    sgtTpeEle->SetAttribute(STARTNUMBER, 1);
    sgtTpeEle->SetAttribute(TIMESCALE, m_timeScale);
    representationEle->InsertEndChild(sgtTpeEle);

    return ERROR_NONE;
}

int32_t MPDWriter::WriteMpd(uint64_t totalFramesNum)
{
    const char *declaration = "xml version=\"1.0\" encoding=\"UTF-8\"";
    XMLDeclaration *xmlDec = m_xmlDoc->NewDeclaration();
    xmlDec->SetValue(declaration);

    m_xmlDoc->InsertFirstChild(xmlDec);

    XMLElement *mpdEle = m_xmlDoc->NewElement(DASH_MPD);
    mpdEle->SetAttribute(OMAF_XMLNS, OMAF_XMLNS_VALUE);
    mpdEle->SetAttribute(XSI_XMLNS, XSI_XMLNS_VALUE);
    mpdEle->SetAttribute(XMLNS, XMLNS_VALUE);
    mpdEle->SetAttribute(XLINK_XMLNS, XLINK_XMLNS_VALUE);
    mpdEle->SetAttribute(XSI_SCHEMALOCATION, XSI_SCHEMALOCATION_VALUE);

    char string[1024];
    memset_s(string, 1024, 0);
    snprintf(string, 1024, "PT%fS", (double)m_segInfo->segDuration);
    mpdEle->SetAttribute(MINBUFFERTIME, string);

    memset_s(string, 1024, 0);
    snprintf(string, 1024, "PT%fS", (double)m_segInfo->segDuration);
    mpdEle->SetAttribute(MAXSEGMENTDURATION, string);

    if (m_segInfo->isLive)
    {
        mpdEle->SetAttribute(PROFILES, PROFILE_LIVE);
        mpdEle->SetAttribute(MPDTYPE, TYPE_LIVE);
    }
    else
    {
        mpdEle->SetAttribute(PROFILES, PROFILE_ONDEMOND);
        mpdEle->SetAttribute(MPDTYPE, TYPE_STATIC);
    }

    if (m_segInfo->isLive)
    {
        uint32_t sec;
        time_t gTime;
        struct tm *t;
        struct timeval now;
        struct timeb timeBuffer;
        ftime(&timeBuffer);
        now.tv_sec = (long)(timeBuffer.time);
        now.tv_usec = timeBuffer.millitm * 1000;
        sec = (uint32_t)(now.tv_sec) + NTP_SEC_1900_TO_1970;

        gTime = sec - NTP_SEC_1900_TO_1970;
        t = gmtime(&gTime);
        if (!t)
            return OMAF_ERROR_INVALID_TIME;

        char forCmp[1024];
        memset_s(forCmp, 1024, 0);
        int32_t cmpRet = 0;
        memcmp_s(m_availableStartTime, 1024, forCmp, 1024, &cmpRet);
        if (0 == cmpRet)
        {
            snprintf(m_availableStartTime, 1024, "%d-%d-%dT%d:%d:%dZ", 1900 + t->tm_year,
                t->tm_mon + 1, t->tm_mday, t->tm_hour, t->tm_min, t->tm_sec);
        }

        if ((0 != cmpRet) && m_cmafEnabled)
        {
            memset_s(m_availableStartTime, 1024, 0);
            snprintf(m_availableStartTime, 1024, "%d-%d-%dT%d:%d:%dZ", 1900 + t->tm_year,
                t->tm_mon + 1, t->tm_mday, t->tm_hour, t->tm_min, t->tm_sec);
        }

        if (!m_publishTime)
        {
            m_publishTime = new char[1024];
            if (!m_publishTime)
                return OMAF_ERROR_NULL_PTR;
        }
        memset_s(m_publishTime, 1024, 0);
        snprintf(m_publishTime, 1024, "%d-%02d-%02dT%02d:%02d:%02dZ", 1900+t->tm_year, t->tm_mon+1, t->tm_mday, t->tm_hour, t->tm_min, t->tm_sec);

        mpdEle->SetAttribute(AVAILABILITYSTARTTIME, m_availableStartTime);
        mpdEle->SetAttribute(TIMESHIFTBUFFERDEPTH, "PT5M");

        memset_s(string, 1024, 0);
        snprintf(string, 1024, "PT%dS", m_miniUpdatePeriod);
        mpdEle->SetAttribute(MINIMUMUPDATEPERIOD, string);
        mpdEle->SetAttribute(PUBLISHTIME, m_publishTime);
    }
    else
    {
        uint32_t fps1000 = (uint32_t) ((double)m_frameRate.num / m_frameRate.den * 1000);
        uint32_t correctedfps = 0;
        if (fps1000 == 29970)
            correctedfps = 30000;
        else if (fps1000 == 23976)
            correctedfps = 24000;
        else if (fps1000 == 59940)
            correctedfps = 60000;
        else
            correctedfps = fps1000;
        uint32_t totalDur = (uint32_t)((double)totalFramesNum * 1000 / ((double)correctedfps / 1000));
        uint32_t hour = totalDur / 3600000;
        totalDur = totalDur % 3600000;
        uint32_t minute = totalDur / 60000;
        totalDur = totalDur % 60000;
        uint32_t second = totalDur / 1000;
        uint32_t msecond = totalDur % 1000;

        if (!m_presentationDur)
        {
            m_presentationDur = new char[1024];
            if (!m_presentationDur)
                return OMAF_ERROR_NULL_PTR;
        }
        memset_s(m_presentationDur, 1024, 0);
        snprintf(m_presentationDur, 1024, "PT%02dH%02dM%02d.%03dS",
            hour, minute, second, msecond);

        mpdEle->SetAttribute(MEDIAPRESENTATIONDURATION, m_presentationDur);
    }

    m_xmlDoc->InsertEndChild(mpdEle);

    XMLElement *essentialEle = m_xmlDoc->NewElement(ESSENTIALPROPERTY);
    essentialEle->SetAttribute(SCHEMEIDURI, SCHEMEIDURI_PF);
    essentialEle->SetAttribute(OMAF_PROJECTIONTYPE, m_projType);
    //xmlDoc.InsertEndChild(essentialEle);
    mpdEle->InsertEndChild(essentialEle);

    if (m_segInfo->baseUrl)
    {
        XMLElement *baseUrlEle = m_xmlDoc->NewElement(BASEURL);
        XMLText *text = m_xmlDoc->NewText(m_segInfo->baseUrl);
        baseUrlEle->InsertEndChild(text);
        mpdEle->InsertEndChild(baseUrlEle);
    }

    if (m_cmafEnabled && m_segInfo->isLive)
    {
        XMLElement *serviceEle = m_xmlDoc->NewElement(SERVICEDESCRIPTION);
        serviceEle->SetAttribute(INDEX, "0");
        mpdEle->InsertEndChild(serviceEle);

        XMLElement *latencyEle = m_xmlDoc->NewElement(LATENCY);
        memset_s(string, 1024, 0);
        snprintf(string, 1024, "%ld", m_segInfo->targetLatency);
        latencyEle->SetAttribute(TARGETLATENCY, string);
        memset_s(string, 1024, 0);
        snprintf(string, 1024, "%ld", m_segInfo->minLatency);
        latencyEle->SetAttribute(MINLATENCY, string);
        memset_s(string, 1024, 0);
        snprintf(string, 1024, "%ld", m_segInfo->maxLatency);
        latencyEle->SetAttribute(MAXLATENCY, string);
        latencyEle->SetAttribute(REFERENCEID, "0");
        serviceEle->InsertEndChild(latencyEle);
    }

    XMLElement *periodEle = m_xmlDoc->NewElement(PERIOD);
    if (m_segInfo->isLive)
    {
        periodEle->SetAttribute(START, "PT0H0M0.000S");
        periodEle->SetAttribute(INDEX, "P1");
    }
    else
    {
        periodEle->SetAttribute(DURATION, m_presentationDur);
    }

    mpdEle->InsertEndChild(periodEle);

    if (m_segInfo->hasMainAS)
    {
        uint16_t maxWidth = 0;
        uint16_t maxHeight = 0;
        uint64_t maxRes = 0;
        uint32_t gopSize = 0;
        std::map<MediaStream*, VCD::MP4::MPDAdaptationSetCtx*>::iterator it = m_streamASCtx->begin();
        for ( ; it != m_streamASCtx->end(); it++)
        {
            MediaStream *stream = it->first;
            if (stream->GetMediaType() == VIDEOTYPE)
            {
                VideoStream *vs = (VideoStream*)stream;
                gopSize = vs->GetGopSize();
                uint16_t width = vs->GetSrcWidth();
                uint16_t height = vs->GetSrcHeight();
                uint64_t resolution = (uint64_t)(width) * (uint64_t)(height);
                if (resolution > maxRes)
                {
                    maxRes = resolution;
                    maxWidth = width;
                    maxHeight = height;
                }
            }
        }

        uint16_t mainWidth = maxWidth;
        uint16_t mainHeight = maxHeight;

        XMLElement *asEle = m_xmlDoc->NewElement(ADAPTATIONSET);
        asEle->SetAttribute(INDEX, 0); //?
        asEle->SetAttribute(MIMETYPE, MIMETYPE_VALUE); //?
        asEle->SetAttribute(CODECS, CODECS_VALUE);
        asEle->SetAttribute(SEGMENTALIGNMENT, 1);
        asEle->SetAttribute(MAXWIDTH, mainWidth);
        asEle->SetAttribute(MAXHEIGHT, mainHeight);
        asEle->SetAttribute(GOPSIZE, gopSize);
        asEle->SetAttribute(BITSTREAMSWITCHING, "false");
        periodEle->InsertEndChild(asEle);

        XMLElement *viewportEle = m_xmlDoc->NewElement(VIEWPORT);
        viewportEle->SetAttribute(SCHEMEIDURI, SCHEMEIDURI_VIEWPORT);
        viewportEle->SetAttribute(COMMON_VALUE, "vpl");
        asEle->InsertEndChild(viewportEle);

        if (m_projType != VCD::OMAF::ProjectionFormat::PF_PLANAR)
        {
            XMLElement *essentialEle1 = m_xmlDoc->NewElement(ESSENTIALPROPERTY);
            essentialEle1->SetAttribute(SCHEMEIDURI, SCHEMEIDURI_SRD);
            essentialEle1->SetAttribute(COMMON_VALUE, "1,0,0,0,0");
            asEle->InsertEndChild(essentialEle1);

            XMLElement *repEle = m_xmlDoc->NewElement(REPRESENTATION);
            repEle->SetAttribute(INDEX, 0);
            repEle->SetAttribute(MIMETYPE, MIMETYPE_VALUE); //?
            repEle->SetAttribute(CODECS, CODECS_VALUE);
            repEle->SetAttribute(WIDTH, mainWidth);
            repEle->SetAttribute(HEIGHT, mainHeight);
            memset_s(string, 1024, 0);
            snprintf(string, 1024, "%ld/%ld", m_frameRate.num, m_frameRate.den);
            repEle->SetAttribute(FRAMERATE, string);
            repEle->SetAttribute(SAR, "1:1");
            repEle->SetAttribute(STARTWITHSAP, 1);
            asEle->InsertEndChild(repEle);

            XMLElement *segTleEle1 = m_xmlDoc->NewElement(SEGMENTTEMPLATE);
            segTleEle1->SetAttribute(TIMESCALE, m_timeScale);
            segTleEle1->SetAttribute(DURATION, m_segInfo->segDuration * m_timeScale);
            segTleEle1->SetAttribute(MEDIA, "track0_$Number$.m4s");
            segTleEle1->SetAttribute(STARTNUMBER, 1);
            repEle->InsertEndChild(segTleEle1);
        }
        else
        {
            XMLElement *essentialEle1 = m_xmlDoc->NewElement(ESSENTIALPROPERTY);
            essentialEle1->SetAttribute(SCHEMEIDURI, SCHEMEIDURI_SRD);
            essentialEle1->SetAttribute(COMMON_VALUE, "1,0,0,0,0");
            asEle->InsertEndChild(essentialEle1);

            XMLElement *supplementalEle = m_xmlDoc->NewElement(SUPPLEMENTALPROPERTY);
            supplementalEle->SetAttribute(SCHEMEIDURI, SCHEMEIDURI_2DQR);
            asEle->InsertEndChild(supplementalEle);

            XMLElement *twoDQualityEle = m_xmlDoc->NewElement(OMAF_TWOD_REGIONQUALITY);
            supplementalEle->InsertEndChild(twoDQualityEle);

            uint32_t currQualityRanking = 1;
            for (currQualityRanking = 1; currQualityRanking <= m_vsNum; currQualityRanking++)
            {
                std::map<MediaStream*, VCD::MP4::MPDAdaptationSetCtx*>::iterator itStr;
                for (itStr = m_streamASCtx->begin(); itStr != m_streamASCtx->end(); itStr++)
                {
                    MediaStream *stream = itStr->first;
                    if (stream && (stream->GetMediaType() == VIDEOTYPE))
                    {
                        VideoStream *vs = (VideoStream*)stream;
                        VCD::MP4::MPDAdaptationSetCtx *asCtx = itStr->second;
                        if (asCtx && (asCtx->qualityRanking == currQualityRanking))
                        {
                            uint16_t width = vs->GetSrcWidth();
                            uint16_t height = vs->GetSrcHeight();
                            uint8_t  tileRows = vs->GetTileInCol();
                            uint8_t  tileCols = vs->GetTileInRow();
                            uint16_t tileWidth = width / tileCols;
                            uint16_t tileHeight = height / tileRows;

                            XMLElement *qualityEle = m_xmlDoc->NewElement(OMAF_QUALITY_INFO);
                            qualityEle->SetAttribute(QUALITY_RANKING, currQualityRanking);
                            qualityEle->SetAttribute(ORIGWIDTH, width);
                            qualityEle->SetAttribute(ORIGHEIGHT, height);
                            qualityEle->SetAttribute(REGIONWIDTH, tileWidth);
                            qualityEle->SetAttribute(REGIONHEIGHT, tileHeight);
                            twoDQualityEle->InsertEndChild(qualityEle);

                            break;
                        }
                    }
                }
            }

            XMLElement *repEle = m_xmlDoc->NewElement(REPRESENTATION);
            repEle->SetAttribute(INDEX, 0);
            repEle->SetAttribute(MIMETYPE, MIMETYPE_VALUE); //?
            repEle->SetAttribute(CODECS, CODECS_VALUE);
            repEle->SetAttribute(WIDTH, mainWidth);
            repEle->SetAttribute(HEIGHT, mainHeight);
            memset_s(string, 1024, 0);
            snprintf(string, 1024, "%ld/%ld", m_frameRate.num, m_frameRate.den);
            repEle->SetAttribute(FRAMERATE, string);
            repEle->SetAttribute(SAR, "1:1");
            repEle->SetAttribute(STARTWITHSAP, 1);
            asEle->InsertEndChild(repEle);

            XMLElement *segTleEle1 = m_xmlDoc->NewElement(SEGMENTTEMPLATE);
            segTleEle1->SetAttribute(TIMESCALE, m_timeScale);
            segTleEle1->SetAttribute(DURATION, m_segInfo->segDuration * m_timeScale);
            segTleEle1->SetAttribute(MEDIA, "track0_$Number$.m4s");
            segTleEle1->SetAttribute(STARTNUMBER, 1);
            repEle->InsertEndChild(segTleEle1);
        }
    }

    std::map<MediaStream*, VCD::MP4::MPDAdaptationSetCtx*>::iterator itTrackCtx;
    for (itTrackCtx = m_streamASCtx->begin();
        itTrackCtx != m_streamASCtx->end();
        itTrackCtx++)
    {
        MediaStream *stream = itTrackCtx->first;

        if (stream && (stream->GetMediaType() == VIDEOTYPE))
        {
            VideoStream *vs = (VideoStream*)stream;
            uint32_t tilesNum = vs->GetTileInRow() * vs->GetTileInCol();
            VCD::MP4::MPDAdaptationSetCtx *trackASCtxs = itTrackCtx->second;
            for (uint32_t i = 0; i < tilesNum; i++)
            {
                WriteTileTrackAS(periodEle, &(trackASCtxs[i]));
            }
        }
        else if (stream && (stream->GetMediaType() == AUDIOTYPE))
        {
            VCD::MP4::MPDAdaptationSetCtx *trackASCtx = itTrackCtx->second;
            WriteAudioTrackAS(periodEle, trackASCtx);
        }
    }

    if (m_extractorASCtx->size())
    {
        std::map<uint32_t, VCD::MP4::MPDAdaptationSetCtx*>::iterator itExtractorCtx;
        for (itExtractorCtx = m_extractorASCtx->begin();
            itExtractorCtx != m_extractorASCtx->end();
            itExtractorCtx++)
        {
            VCD::MP4::MPDAdaptationSetCtx *trackASCtx = itExtractorCtx->second;
            WriteExtractorTrackAS(periodEle, trackASCtx);
        }
    }

    m_xmlDoc->SaveFile(m_mpdFileName);

    return ERROR_NONE;
}

int32_t MPDWriter::UpdateMpd(uint64_t segNumber, uint64_t framesNumber)
{
    m_currSegNum = segNumber;
    if (m_segInfo->windowSize)
    {
        if (segNumber % m_segInfo->windowSize == 1)
        {
            char buf[PATH_MAX] = { 0 };
            char *res = realpath(m_mpdFileName, buf);
            if (res)
            {
                remove(buf);
                DELETE_MEMORY(m_xmlDoc);

                m_xmlDoc = new XMLDocument;
                if (!m_xmlDoc)
                    return OMAF_ERROR_CREATE_XMLFILE_FAILED;
            }
            else
            {
                if (segNumber > 1)
                {
                    perror("realpath");
                    return OMAF_ERROR_REALPATH_FAILED;
                }
            }

            int32_t ret = WriteMpd(framesNumber);
            return ret;
        }
    }
    else
    {
        if (framesNumber % (m_segInfo->segDuration * (uint16_t)((double)(m_frameRate.num / m_frameRate.den) + 0.5)) == 0)
        {
            char buf[PATH_MAX] = { 0 };
            char *res = realpath(m_mpdFileName, buf);
            if (res)
            {
                remove(buf);
                DELETE_MEMORY(m_xmlDoc);

                m_xmlDoc = new XMLDocument;
                if (!m_xmlDoc)
                    return OMAF_ERROR_CREATE_XMLFILE_FAILED;
            }
            else
            {
                perror("realpath");
                return OMAF_ERROR_REALPATH_FAILED;
            }

            int32_t ret = WriteMpd(framesNumber);
            return ret;
        }
    }

    return ERROR_NONE;
}

extern "C" MPDWriterBase* Create(
    std::map<MediaStream*, VCD::MP4::MPDAdaptationSetCtx*> *streamsASCtxs,
    std::map<uint32_t, VCD::MP4::MPDAdaptationSetCtx*> *extractorASCtxs,
    SegmentationInfo *segInfo,
    VCD::OMAF::ProjectionFormat projType,
    Rational frameRate,
    uint8_t  videoNum,
    bool     cmafEnabled)
{
    MPDWriter *mpdWriter = new MPDWriter(streamsASCtxs, extractorASCtxs, segInfo, projType, frameRate, videoNum, cmafEnabled);
    return (MPDWriterBase*)(mpdWriter);
}

extern "C" void Destroy(MPDWriterBase* mpdWriter)
{
    delete mpdWriter;
    mpdWriter = NULL;
}
