#include "TSParser.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>

#define MAX_READ_PKT_NUM                100
#define MAX_CHECK_PKT_NUM               10

#define MK_WORD(high,low)               (((high)<<8)|(low))
#define TIMESTAMP(b1,b2,b3,b4,b5)       (((sint64)(b1)<<25)|((sint64)(b2)<<17)|((sint64)(b3)<<9)|((sint64)(b4)<<1)|(b5))

#define MIN(a,b)                        (((a) < (b)) ? (a) : (b))
#define RETURN_IF_NOT_OK(ret)           if (TS_OK != ret) { return ret; }
#define PRINT(fmt,...)                  printf(fmt, ## __VA_ARGS__)
#define PRINT_LINE(fmt,...)             printf(fmt" -- [%s:%d]\n", ## __VA_ARGS__, __FILE__, __LINE__)

uint16 TSPacket::s_au16PIDs[E_MAX] = {PID_UNSPEC,PID_UNSPEC,PID_UNSPEC,PID_UNSPEC}; // ��¼����pid

/*****************************************************************************
 * �� �� ��  : TSPacket.Parse
 * ��������  : ����TS��
 * ��    ��  : const char *pBuf  
               uint16 u16BufLen  
 * �� �� ֵ  : TS_ERR
 * ��    ��  : JiaSong
 * ��������  : 2015-8-29
*****************************************************************************/
TS_ERR TSPacket::Parse(const char *pBuf, uint16 u16BufLen)
{
    assert(NULL != pBuf);

    TS_ERR ret = TS_OK;
    if ((NULL == pBuf) || (TS_PKT_LEN != u16BufLen))
    {
        return TS_IN_PARAM_ERR;
    }

    if (TS_SYNC_BYTE != pBuf[0])
    {
        return TS_SYNC_BYTE_ERR;
    }

    m_pBuf = pBuf;

    m_pHdr = (TSHdrFixedPart*)pBuf;
    m_u16PID = MK_WORD(m_pHdr->pid12_8,m_pHdr->pid7_0);
    m_u8CC   = m_pHdr->continuity_counter;

    if (IsPAT())
    {
        ret = __ParsePAT();
    }
    else if (IsPMT())
    {
        ret = __ParsePMT();
    }
    else if (m_u16PID == s_au16PIDs[E_PCR])
    {
        m_s64PCR = __GetPCR();
    }
    
    if (m_pHdr->payload_unit_start_indicator)
    {
        ret = __ParsePES();
    }

    return ret;
}

/*****************************************************************************
 * �� �� ��  : TSPacket.__HasAdaptField
 * ��������  : �ж��Ƿ������Ӧ����
 * ��    ��  : ��
 * �� �� ֵ  : bool
 * ��    ��  : JiaSong
 * ��������  : 2015-8-29
*****************************************************************************/
bool TSPacket::__HasAdaptField()
{
    assert(NULL != m_pHdr);
    return 0 != (m_pHdr->adaptation_field_control & 0x2);
}

/*****************************************************************************
 * �� �� ��  : TSPacket.__HasPayload
 * ��������  : �ж��Ƿ���ڸ���
 * ��    ��  : ��
 * �� �� ֵ  : bool
 * ��    ��  : JiaSong
 * ��������  : 2015-8-29
*****************************************************************************/
bool TSPacket::__HasPayload()
{
    assert(NULL != m_pHdr);
    return m_pHdr->payload_unit_start_indicator || (m_pHdr->adaptation_field_control & 0x1);
}

/*****************************************************************************
 * �� �� ��  : TSPacket.__GetAdaptField
 * ��������  : ��ȡ��Ӧ����ָ��;��Ӧ���򲻴���ʱ����NULL
 * ��    ��  : ��
 * �� �� ֵ  : AdaptFixedPart*
 * ��    ��  : JiaSong
 * ��������  : 2015-8-29
*****************************************************************************/
AdaptFixedPart* TSPacket::__GetAdaptField()
{
    assert(NULL != m_pBuf);
    assert(NULL != m_pHdr);

    AdaptFixedPart *pAdpt = NULL;

    if (__HasAdaptField())
    {
        pAdpt = (AdaptFixedPart*)(m_pBuf + sizeof(TSHdrFixedPart));
    }

    return pAdpt;
}

/*****************************************************************************
 * �� �� ��  : TSPacket.__GetAdaptLen
 * ��������  : ��ȡ��Ӧ����ĳ���
 * ��    ��  : ��
 * �� �� ֵ  : uint8
 * ��    ��  : JiaSong
 * ��������  : 2015-8-29
*****************************************************************************/
uint8 TSPacket::__GetAdaptLen()
{
    uint8 u8AdaptLen = 0;

    AdaptFixedPart *pAdpt = __GetAdaptField();

    if (NULL != pAdpt)
    {
        // "adaptation_field_length" field is 1 byte
        u8AdaptLen = pAdpt->adaptation_field_length + 1;
    }

    return u8AdaptLen;
}

/*****************************************************************************
 * �� �� ��  : TSPacket.__GetPCR
 * ��������  : ����PCR�ֶ�ʱ,��ȡPCR��ֵ;������ʱ����-1
 * ��    ��  : ��
 * �� �� ֵ  : sint64
 * ��    ��  : JiaSong
 * ��������  : 2015-8-29
*****************************************************************************/
sint64 TSPacket::__GetPCR()
{
    assert(NULL != m_pBuf);
    assert(NULL != m_pHdr);

    sint64 s64PCR = 0;
    if (__HasAdaptField())
    {
        AdaptFixedPart *pAdpt = (AdaptFixedPart*)(m_pBuf + sizeof(TSHdrFixedPart));
        if (pAdpt->PCR_flag)
        {
            PCR *pcr = (PCR*)((const char*)pAdpt + sizeof(AdaptFixedPart));
            s64PCR = TIMESTAMP(pcr->pcr_base32_25,
                                pcr->pcr_base24_17,
                                pcr->pcr_base16_9,
                                pcr->pcr_base8_1,
                                pcr->pcr_base0);
        }
    }
    return s64PCR;
}

/*****************************************************************************
 * �� �� ��  : TSPacket.__IsVideoStream
 * ��������  : ����StreamType�ж��Ƿ���Ƶ��
 * ��    ��  : uint8 u8StreamType  
 * �� �� ֵ  : bool
 * ��    ��  : JiaSong
 * ��������  : 2015-8-29
*****************************************************************************/
bool TSPacket::__IsVideoStream(uint8 u8StreamType)
{
    return ((ES_TYPE_MPEG1V == u8StreamType)
        || (ES_TYPE_MPEG2V == u8StreamType)
        || (ES_TYPE_MPEG4V == u8StreamType)
        || (ES_TYPE_H264 == u8StreamType));
}

/*****************************************************************************
 * �� �� ��  : TSPacket.__IsAudioStream
 * ��������  : ����StreamType�ж��Ƿ���Ƶ��
 * ��    ��  : uint8 u8StreamType  
 * �� �� ֵ  : bool
 * ��    ��  : JiaSong
 * ��������  : 2015-8-29
*****************************************************************************/
bool TSPacket::__IsAudioStream(uint8 u8StreamType)
{
    return ((ES_TYPE_MPEG1A == u8StreamType)
        || (ES_TYPE_MPEG2A == u8StreamType)
        || (ES_TYPE_AC3 == u8StreamType)
        || (ES_TYPE_AAC == u8StreamType)
        || (ES_TYPE_DTS == u8StreamType));
}

/*****************************************************************************
 * �� �� ��  : TSPacket.__GetPayloadOffset
 * ��������  : ��ȡ���������TS��ͷ��ƫ��
 * ��    ��  : ��
 * �� �� ֵ  : uint8
 * ��    ��  : JiaSong
 * ��������  : 2015-8-29
*****************************************************************************/
uint8 TSPacket::__GetPayloadOffset()
{
    uint8 u8Pos = sizeof(TSHdrFixedPart);
    if (__HasAdaptField())
    {
        u8Pos += __GetAdaptLen();;
    }
    return u8Pos;
}

/*****************************************************************************
 * �� �� ��  : TSPacket.__GetTableStartPos
 * ��������  : ��ȡPAT/PMT�������TS��ͷ��ƫ��
 * ��    ��  : ��
 * �� �� ֵ  : uint8
 * ��    ��  : JiaSong
 * ��������  : 2015-8-29
*****************************************************************************/
uint8 TSPacket::__GetTableStartPos()
{
    assert(NULL != m_pBuf);

    uint8 u8Pos = __GetPayloadOffset();
    if (__HasPayload())
    {
        // "pointer_field" field is 1 byte,
        // and whose value is the number of bytes before payload
        uint8 u8PtrFieldLen = m_pBuf[u8Pos] + 1;
        u8Pos += u8PtrFieldLen;
    }
    return u8Pos;
}

/*****************************************************************************
 * �� �� ��  : TSPacket.__GetPTS
 * ��������  : ����PTS�ֶ�ʱ,��ȡPTS��ֵ;������ʱ����-1
 * ��    ��  : const OptionPESHdrFixedPart *pHdr  
 * �� �� ֵ  : sint64
 * ��    ��  : JiaSong
 * ��������  : 2015-8-29
*****************************************************************************/
sint64 TSPacket::__GetPTS(const OptionPESHdrFixedPart *pHdr)
{
    assert(NULL != pHdr);

    sint64 s64PTS = INVALID_VAL;
    if (pHdr->PTS_DTS_flags & 0x2)
    {
        PTS_DTS *pPTS = (PTS_DTS*)((char*)pHdr + sizeof(OptionPESHdrFixedPart));
        s64PTS = TIMESTAMP(pPTS->ts32_30, pPTS->ts29_22, pPTS->ts21_15, pPTS->ts14_7, pPTS->ts6_0);
    }

    return s64PTS;
}

/*****************************************************************************
 * �� �� ��  : TSPacket.__GetDTS
 * ��������  : ����DTS�ֶ�ʱ,��ȡDTS��ֵ;������ʱ����-1
 * ��    ��  : const OptionPESHdrFixedPart *pHdr  
 * �� �� ֵ  : sint64
 * ��    ��  : JiaSong
 * ��������  : 2015-8-29
*****************************************************************************/
sint64 TSPacket::__GetDTS(const OptionPESHdrFixedPart *pHdr)
{
    assert(NULL != pHdr);

    sint64 s64DTS = INVALID_VAL;
    if (pHdr->PTS_DTS_flags & 0x1)
    {
        PTS_DTS *pDTS = (PTS_DTS*)((char*)pHdr + sizeof(OptionPESHdrFixedPart) + sizeof(PTS_DTS));
        s64DTS = TIMESTAMP(pDTS->ts32_30, pDTS->ts29_22, pDTS->ts21_15, pDTS->ts14_7, pDTS->ts6_0);
    }
    
    return s64DTS;
}

/*****************************************************************************
 * �� �� ��  : TSPacket.__ParsePAT
 * ��������  : ����PAT��,��ȡPMT��Ϣ
 * ��    ��  : ��
 * �� �� ֵ  : TS_ERR
 * ��    ��  : JiaSong
 * ��������  : 2015-8-29
*****************************************************************************/
TS_ERR TSPacket::__ParsePAT()
{
    assert(NULL != m_pBuf);

    const char *pPATBuf = m_pBuf + __GetTableStartPos();
    PATHdrFixedPart *pPAT = (PATHdrFixedPart*)pPATBuf;
    uint16 u16SectionLen = MK_WORD(pPAT->section_length11_8, pPAT->section_length7_0);
    uint16 u16AllSubSectionLen = u16SectionLen - sizeof(PATHdrFixedPart) - CRC32_LEN;

    uint16 u16SubSectionLen = sizeof(PATSubSection);
    const char *ptr = pPATBuf + sizeof(PATHdrFixedPart);
    for (uint16 i = 0; i < u16AllSubSectionLen; i+= u16SubSectionLen)
    {
        PATSubSection *pDes = (PATSubSection*)(ptr + i);
        uint16 u16ProgNum = pDes->program_number;
        uint16 u16PID = MK_WORD(pDes->pid12_8, pDes->pid7_0);
        if (0x00 == u16ProgNum)
        {
            uint16 u16NetworkPID = u16PID;
        }
        else
        {
            m_u16PMTPID = u16PID;// program_map_PID
            break;
        }
    }

    s_au16PIDs[E_PMT] = m_u16PMTPID;
    return TS_OK;
}

/*****************************************************************************
 * �� �� ��  : TSPacket.__ParsePMT
 * ��������  : ����PMT��,��ȡPCR,��Ƶ����Ƶpid��Ϣ
 * ��    ��  : ��
 * �� �� ֵ  : TS_ERR
 * ��    ��  : JiaSong
 * ��������  : 2015-8-29
*****************************************************************************/
TS_ERR TSPacket::__ParsePMT()
{
    assert(NULL != m_pBuf);

    const char *pPMTBuf = m_pBuf + __GetTableStartPos();
    PMTHdrFixedPart *pPMT = (PMTHdrFixedPart*)pPMTBuf;
    s_au16PIDs[E_PCR] = MK_WORD(pPMT->PCR_PID12_8, pPMT->PCR_PID7_0);
    uint16 u16SectionLen = MK_WORD(pPMT->section_length11_8, pPMT->section_length7_0);
    // n * program_info_descriptor�ĳ���
    uint16 u16ProgInfoLen = MK_WORD(pPMT->program_info_length11_8, pPMT->program_info_length7_0);
    uint16 u16AllSubSectionLen = u16SectionLen - (sizeof(PMTHdrFixedPart) - 3) - u16ProgInfoLen - CRC32_LEN;

    uint16 u16SubSectionLen = sizeof(PMTSubSectionFixedPart);
    const char *ptr = pPMTBuf + sizeof(PMTHdrFixedPart) + u16ProgInfoLen;
    for (uint16 i = 0; i < u16AllSubSectionLen; i += u16SubSectionLen)
    {
        PMTSubSectionFixedPart *pSec = (PMTSubSectionFixedPart*)(ptr + i);
        uint16 u16ElementaryPID = MK_WORD(pSec->elementaryPID12_8, pSec->elementaryPID7_0);
        uint16 u16ESInfoLen = MK_WORD(pSec->ES_info_lengh11_8, pSec->ES_info_lengh7_0);
        u16SubSectionLen += u16ESInfoLen;

        if (__IsVideoStream(pSec->stream_type))
        {
            s_au16PIDs[E_VIDEO] = u16ElementaryPID;
        }
        else if (__IsAudioStream(pSec->stream_type))
        {
            s_au16PIDs[E_AUDIO] = u16ElementaryPID;
        }
    }
    return TS_OK;
}

/*****************************************************************************
 * �� �� ��  : TSPacket.__ParsePES
 * ��������  : ����PES,��ȡPTS��DTS
 * ��    ��  : ��
 * �� �� ֵ  : TS_ERR
 * ��    ��  : JiaSong
 * ��������  : 2015-8-29
*****************************************************************************/
TS_ERR TSPacket::__ParsePES()
{
    assert(NULL != m_pBuf);

    const char *pPESBuf = m_pBuf + __GetPayloadOffset();
    PESHdrFixedPart *pPES = (PESHdrFixedPart*)pPESBuf;
    
    if (PES_START_CODE == pPES->packet_start_code_prefix)
    {
        m_u8StreamId = pPES->stream_id;        
        if ((m_u8StreamId & PES_STREAM_VIDEO) || (m_u8StreamId & PES_STREAM_AUDIO))
        {
            OptionPESHdrFixedPart *pHdr = (OptionPESHdrFixedPart*)(pPESBuf + sizeof(PESHdrFixedPart));
            m_s64PTS = __GetPTS(pHdr);
            m_s64DTS = __GetDTS(pHdr);
        }
    }
    return TS_OK;
}

TSParser::TSParser(const char *pFilePath) : m_strFile(""), m_pFd(NULL)
{
    m_strFile = pFilePath;
}
TSParser::~TSParser()
{
    __CloseFile();
}

/*****************************************************************************
 * �� �� ��  : TSParser.Parse
 * ��������  : ����TS�ļ�
 * ��    ��  : ��
 * �� �� ֵ  : TS_ERR
 * ��    ��  : JiaSong
 * ��������  : 2015-8-29
*****************************************************************************/
TS_ERR TSParser::Parse()
{
    TS_ERR ret = TS_OK;
    ret = __OpenFile();
    RETURN_IF_NOT_OK(ret);

    if (!__SeekToFirstPkt())
    {
        PRINT_LINE("Seek to first packet failed!");
        return TS_FILE_SEEK_FAIL;
    }

    sint64 s64CurPos = ftello64(m_pFd);
    PRINT_LINE("Seek to first packet, offset: 0x%08llX", s64CurPos);

    uint16 u16ReadBufLen = MAX_READ_PKT_NUM * TS_PKT_LEN;
    char *pReadBuf = new char[u16ReadBufLen];
    AutoDelCharBuf tBuf(pReadBuf);
    while (0 == feof(m_pFd))
    {
        sint16 s16ReadLen = fread(pReadBuf, 1, u16ReadBufLen, m_pFd);
        if (s16ReadLen >= 0)
        {
            uint16 u16Count = s16ReadLen / TS_PKT_LEN;
            for (uint16 i = 0; i < u16Count; i++)
            {
                TSPacket tPkt;
                ret = tPkt.Parse(pReadBuf + i*TS_PKT_LEN, TS_PKT_LEN);
                RETURN_IF_NOT_OK(ret);

                __PrintPacketInfo(tPkt, s64CurPos);
                s64CurPos += TS_PKT_LEN;
            }
        }
        else
        {
            PRINT_LINE("###### Read file error, ret<%d>", s16ReadLen);
            break;
        }
    }

    PRINT_LINE("Parse file complete!");
    return ret;
}

/*****************************************************************************
 * �� �� ��  : TSParser.__OpenFile
 * ��������  : ��TS�ļ�
 * ��    ��  : ��
 * �� �� ֵ  : TS_ERR
 * ��    ��  : JiaSong
 * ��������  : 2015-8-29
*****************************************************************************/
TS_ERR TSParser::__OpenFile()
{
    m_pFd = fopen(m_strFile.c_str(), "rb");
    if (NULL == m_pFd)
    {
        PRINT_LINE("###### Open file<%s> failed! errno<%d>", m_strFile.c_str(), errno);
        return TS_FILE_OPEN_FAIL;
    }

    PRINT_LINE("Open file<%s> success.", m_strFile.c_str());
    return TS_OK;
}

/*****************************************************************************
 * �� �� ��  : TSParser.__CloseFile
 * ��������  : �ر��ļ�
 * ��    ��  : ��
 * �� �� ֵ  : TS_ERR
 * ��    ��  : JiaSong
 * ��������  : 2015-8-29
*****************************************************************************/
TS_ERR TSParser::__CloseFile()
{
    if (NULL != m_pFd)
    {
        fclose(m_pFd);
        m_pFd = NULL;
        PRINT_LINE("Close file<%s>", m_strFile.c_str());
    }
    return TS_OK;
}

/*****************************************************************************
 * �� �� ��  : TSParser.__SeekToFirstPkt
 * ��������  : ���ļ���ȡָ��ƫ������һ���Ϸ���TS��
 * ��    ��  : ��
 * �� �� ֵ  : bool
 * ��    ��  : JiaSong
 * ��������  : 2015-8-29
*****************************************************************************/
bool TSParser::__SeekToFirstPkt()
{
    uint16 u16ReadBufLen = MAX_READ_PKT_NUM * TS_PKT_LEN;
    char *pReadBuf = new char[u16ReadBufLen];
    AutoDelCharBuf tBuf(pReadBuf);
    sint16 s16ReadLen = fread(pReadBuf, 1, u16ReadBufLen, m_pFd);
    if (s16ReadLen > 0)
    {
        uint16 u16PktCount = s16ReadLen / TS_PKT_LEN;
        uint16 u16Count = MIN(MAX_CHECK_PKT_NUM, u16PktCount);
        for (uint16 i = 0; i < s16ReadLen - u16Count*TS_PKT_LEN; i++)
        {
            if (TS_SYNC_BYTE == pReadBuf[i])
            {
                uint16 n = 0;
                for (; n < u16Count; n++)
                {
                    if (TS_SYNC_BYTE != pReadBuf[i + n*TS_PKT_LEN])
                    {
                        break;
                    }
                }

                if (u16Count == n)
                {
                    return (0 == fseek(m_pFd, i, SEEK_SET));
                }
            }
        }
    }
    else
    {
        PRINT_LINE("###### Read file error, ret<%d>", s16ReadLen);
    }

    return false;
}

/*****************************************************************************
 * �� �� ��  : TSParser.__PrintPacketInfo
 * ��������  : ��ӡTS������Ϣ
 * ��    ��  : TSPacket &tPkt    
               uint64 u64Offset  
 * �� �� ֵ  : void
 * ��    ��  : JiaSong
 * ��������  : 2015-8-29
*****************************************************************************/
void TSParser::__PrintPacketInfo(TSPacket &tPkt, uint64 u64Offset)
{
    static uint32 s_u32PktNo = 0;
    PRINT("PktNo: %08u, Offset: 0x%08llX, PID: 0x%04X, CC: %02u",
          s_u32PktNo++, u64Offset, tPkt.GetPID(), tPkt.GetCC());
    
    if (tPkt.IsPAT())
    {
        PRINT(", PAT");
    }
    else if (tPkt.IsPMT())
    {
        PRINT(", PMT");
    }
    else if (tPkt.GetPCR() > 0)
    {
        PRINT(", PCR: %lld", tPkt.GetPCR());
    }
    else if (PID_NULL == tPkt.GetPID())
    {
        PRINT(", Null Packet");
    }    

    if (tPkt.GetPTS() > 0)
    {
        PRINT(", PTS: %lld", tPkt.GetPTS());
    }
    if (tPkt.GetDTS() > 0)
    {
        PRINT(", DTS: %lld", tPkt.GetDTS());
    }

    if (tPkt.IsVideo())
    {
        PRINT(", Video");
    }
    else if (tPkt.IsAudio())
    {
        PRINT(", Audio");        
    }

    PRINT_LINE("");
}

