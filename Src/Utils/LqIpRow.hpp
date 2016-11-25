#pragma once
/*
* Lanq(Lan Quick)
* Solodov A. N. (hotSAN)
* 2016
*   LqIpRow - .
*/

#include "LqOs.h"
#include "LqPtdArr.hpp"


#pragma pack(push)
#pragma pack(1)

template<typename NumT, bool ByteOrderBigEndiean>
class LqByteOrderNum
{
    NumT val;
public:
    typedef NumT Type;
    operator NumT() const
    {
        NumT v;
        if(ByteOrderBigEndiean)
        {
            for(intptr_t i = 0; i < sizeof(NumT); i++)
                v = (v << 8) | ((uint8_t*)&val)[i];
        } else
        {
            for(intptr_t i = sizeof(NumT) - 1; i >= 0; i--)
                v = (v << 8) | ((uint8_t*)&val)[i];
        }
        return v;
    }

    NumT operator=(NumT n)
    {
        NumT v = n;
        if(ByteOrderBigEndiean)
        {
            for(intptr_t i = sizeof(NumT) - 1; i >= 0; i--, v >>= 8)
                ((uint8_t*)&val)[i] = v & 0xff;
        } else
        {
            for(intptr_t i = 0; i < sizeof(NumT); i++, v >>= 8)
                ((uint8_t*)&val)[i] = v & 0xff;
        }
        return n;
    }
};

template<typename NumT, size_t BitOff, size_t BitLen>
class LqBitField
{
    NumT val;
    static const NumT Filt1 = (NumT(-1) >> BitOff) & (NumT(-1) << ((sizeof(NumT) * 8) - (BitOff + BitLen)));
    static const NumT Filt2 = ~Filt1;
    static const NumT ShrB = (sizeof(NumT) * 8) - (BitOff + BitLen);
public:
    operator NumT() { return (val & Filt1) >> ShrB; }
    NumT operator=(NumT n) { val = (val & Filt2) | (Filt1 & (n << ShrB)); return n; }
};

template<typename T, bool ByteOrderBigEndiean, size_t BitOff, size_t BitLen>
class LqBitField<LqByteOrderNum<T, ByteOrderBigEndiean>, BitOff, BitLen>
{
    LqByteOrderNum<T, ByteOrderBigEndiean> val;
    static const T Filt1 = (T(-1) >> BitOff) & (T(-1) << ((sizeof(T) * 8) - (BitOff + BitLen)));
    static const T Filt2 = ~Filt1;
    static const T ShrB = (sizeof(T) * 8) - (BitOff + BitLen);
public:
    operator T() { return (val & Filt1) >> ShrB; }
    T operator=(T n) { val = (val & Filt2) | (Filt1 & (n << ShrB)); return n; }
};

struct ipheader
{
    union
    {
        LqBitField<LqByteOrderNum<uint8_t, true>, 0, 4> version;
        LqBitField<LqByteOrderNum<uint8_t, true>, 4, 4> ihl;
    };
    union
    {
        LqBitField<LqByteOrderNum<uint8_t, true>, 0, 6> dscp;
        LqBitField<LqByteOrderNum<uint8_t, true>, 6, 2> ecn;
    };

    LqByteOrderNum<uint16_t, true> len;
    LqByteOrderNum<uint16_t, true> id;
    union
    {
        LqBitField<LqByteOrderNum<uint16_t, true>, 0, 1> reserv;
        LqBitField<LqByteOrderNum<uint16_t, true>, 1, 1> nofr;
        LqBitField<LqByteOrderNum<uint16_t, true>, 2, 1> hasfr;
        LqBitField<LqByteOrderNum<uint16_t, true>, 3, 13> offset;
    };
    uint8_t  ttl;
    uint8_t  protocol;
    LqByteOrderNum<uint16_t, true> check;
    LqByteOrderNum<uint32_t, true> saddr;
    LqByteOrderNum<uint32_t, true> daddr;


    int ToString(char* DestBuf, size_t DestBufSize)
    {
        return snprintf(
            DestBuf,
            DestBufSize,
            "version: %u\n"
            "hdr_len: %u\n"
            "dscp: %u\n"
            "ecn: %u\n"
            "len: %u\n"
            "id: %u\n"
            "flags: nofr=%u, hasfr=%u\n"
            "offset: %u\n"
            "ttl: %u\n"
            "protocol: %u\n"
            "check: %u\n"
            "saddr: %u.%u.%u.%u\n"
            "daddr: %u.%u.%u.%u\n",
            (unsigned)version,
            (unsigned)ihl,
            (unsigned)dscp,
            (unsigned)ecn,
            (unsigned)len,
            (unsigned)id,
            (unsigned)nofr,
            (unsigned)hasfr,
            (unsigned)offset,
            (unsigned)ttl,
            (unsigned)protocol,
            (unsigned)check,
            (unsigned)(((uint8_t*)&saddr)[0]),
            (unsigned)(((uint8_t*)&saddr)[1]),
            (unsigned)(((uint8_t*)&saddr)[2]),
            (unsigned)(((uint8_t*)&saddr)[3]),
            (unsigned)(((uint8_t*)&daddr)[0]),
            (unsigned)(((uint8_t*)&daddr)[1]),
            (unsigned)(((uint8_t*)&daddr)[2]),
            (unsigned)(((uint8_t*)&daddr)[3])
        );
    }

    void ComputeChecksum()
    {
        check = 0;
        unsigned long sum = 0;
        size_t count = ihl * 4;
        uint16_t* addr = (uint16_t*)this;
        for(sum = 0; count > 1; count -= 2)
            sum += *addr++;
        if(count == 1)
            sum += (char)*addr;
        sum = (sum >> 16) + (sum & 0xFFFF);
        sum += (sum >> 16);
        *(uint16_t*)&check = ~sum;
    }
};


struct tcphdr
{
    LqByteOrderNum<uint16_t, true> source;
    LqByteOrderNum<uint16_t, true> dest;
    LqByteOrderNum<uint32_t, true> seq;
    LqByteOrderNum<uint32_t, true> ack_seq;
    union
    {
        LqBitField<LqByteOrderNum<uint16_t, true>, 0, 4> thl;
        LqBitField<LqByteOrderNum<uint16_t, true>, 4, 3> res1;
        LqBitField<LqByteOrderNum<uint16_t, true>, 7, 1> ns;

        LqBitField<LqByteOrderNum<uint16_t, true>, 8, 1> cwr;
        LqBitField<LqByteOrderNum<uint16_t, true>, 9, 1> ece;
        LqBitField<LqByteOrderNum<uint16_t, true>, 10, 1> urg;
        LqBitField<LqByteOrderNum<uint16_t, true>, 11, 1> ack;
        LqBitField<LqByteOrderNum<uint16_t, true>, 12, 1> psh;
        LqBitField<LqByteOrderNum<uint16_t, true>, 13, 1> rst;
        LqBitField<LqByteOrderNum<uint16_t, true>, 14, 1> syn;
        LqBitField<LqByteOrderNum<uint16_t, true>, 15, 1> fin;
    };
    LqByteOrderNum<uint16_t, true>   window;
    LqByteOrderNum<uint16_t, true>   check;
    LqByteOrderNum<uint16_t, true>   urg_ptr;

    int ToString(char* DestBuf, size_t DestBufSize)
    {
        return snprintf(
            DestBuf,
            DestBufSize,
            "source: %u\n"
            "dest: %u\n"
            "seq: %u\n"
            "ack_seq: %u\n"
            "thl: %u\n"
            "res1: %u\n"
            "flags: cwr=%u, ece=%u, urg=%u, ack=%u, psh=%u, rst=%u, syn=%u, fin=%u\n"
            "window: %u\n"
            "check: %u\n"
            "urg_ptr: %u\n",
            (unsigned)source,
            (unsigned)dest,
            (unsigned)seq,
            (unsigned)ack_seq,
            (unsigned)thl,
            (unsigned)res1,
            (unsigned)cwr,
            (unsigned)ece,
            (unsigned)urg,
            (unsigned)ack,
            (unsigned)psh,
            (unsigned)rst,
            (unsigned)syn,
            (unsigned)fin,
            (unsigned)window,
            (unsigned)check,
            (unsigned)urg_ptr
        );
    }


    void ComputeChecksum(ipheader *iph, size_t DataLen = 0, void* Data = nullptr)
    {
        const uint16_t *buf = (uint16_t*)this;
        uint32_t sum = 0;
        int len = thl * 4;
        *(uint16_t*)&check = 0;
        for(; len > 1; len -= 2)
            sum += *(buf++);
        if(Data == nullptr)
        {
            len = DataLen;
            for(; len > 1; len -= 2)
                sum += *(buf++);
        } else
        {
            if(len == 1)
                sum += *((uint8_t *)(buf++));
            len = DataLen;
            buf = (uint16_t*)Data;
            for(; len > 1; len -= 2)
                sum += *(buf++);
        }
        if(len == 1)
            sum += *((uint8_t *)buf);
        sum += (*((uint32_t*)&iph->saddr) >> 16) & 0xffff;
        sum += (*((uint32_t*)&iph->saddr) & 0xffff);
        sum += (*((uint32_t*)&iph->daddr) >> 16) & 0xffff;
        sum += (*((uint32_t*)&iph->daddr) & 0xffff);
        sum += htons(iph->protocol);
        sum += htons(thl * 4 + DataLen);
        while(sum >> 16)
            sum = (sum & 0xFFFF) + (sum >> 16);
        *(uint16_t*)&check = (uint16_t)(~sum);
    }
    void ComputeChecksum(size_t DataLen = 0, void* Data = nullptr)
    {
        const uint16_t *buf = (uint16_t*)this;
        uint32_t sum = 0;
        int len = thl * 4;
        *(uint16_t*)&check = 0;
        for(; len > 1; len -= 2)
            sum += *(buf++);
        len = DataLen;
        if(Data == nullptr)
        {
            for(; len > 1; len -= 2)
                sum += *(buf++);
        } else
        {
            if(len == 1)
                sum += *((uint8_t *)(buf++));
            buf = (uint16_t*)Data;
            for(; len > 1; len -= 2)
                sum += *(buf++);
        }
        if(len == 1)
            sum += *((uint8_t *)buf);
        sum += htons(thl * 4 + DataLen);
        while(sum >> 16)
            sum = (sum & 0xFFFF) + (sum >> 16);
        *(uint16_t*)&check = (uint16_t)(~sum);
    }
};


struct udphdr
{
    LqByteOrderNum<uint16_t, true> source;
    LqByteOrderNum<uint16_t, true> dest;
    LqByteOrderNum<uint16_t, true> len;
    LqByteOrderNum<uint16_t, true> check;

    int ToString(char* DestBuf, size_t DestBufSize)
    {
        return snprintf(
            DestBuf,
            DestBufSize,
            "source: %u\n"
            "dest: %u\n"
            "len: %u\n"
            "check: %u\n",
            (unsigned)source,
            (unsigned)dest,
            (unsigned)len,
            (unsigned)check
        );
    }


    void ComputeChecksum(ipheader *iph, size_t DataLen = 0, void* Data = nullptr)
    {
        const uint16_t *buf = (uint16_t*)this;
        uint32_t sum = 0;
        int len = sizeof(udphdr);
        *(uint16_t*)&check = 0;
        for(; len > 1; len -= 2)
            sum += *(buf++);
        if(Data == nullptr)
        {
            len = DataLen;
            for(; len > 1; len -= 2)
                sum += *(buf++);
        } else
        {
            if(len == 1)
                sum += *((uint8_t *)(buf++));
            len = DataLen;
            buf = (uint16_t*)Data;
            for(; len > 1; len -= 2)
                sum += *(buf++);
        }
        if(len == 1)
            sum += *((uint8_t *)buf);
        sum += (*((uint32_t*)&iph->saddr) >> 16) & 0xffff;
        sum += (*((uint32_t*)&iph->saddr) & 0xffff);
        sum += (*((uint32_t*)&iph->daddr) >> 16) & 0xffff;
        sum += (*((uint32_t*)&iph->daddr) & 0xffff);
        sum += htons(iph->protocol);
        sum += htons(sizeof(udphdr) + DataLen);
        while(sum >> 16)
            sum = (sum & 0xFFFF) + (sum >> 16);
        *(uint16_t*)&check = (uint16_t)(~sum);
    }
    void ComputeChecksum(size_t DataLen = 0, void* Data = nullptr)
    {
        const uint16_t *buf = (uint16_t*)this;
        uint32_t sum = 0;
        int len = sizeof(udphdr);
        *(uint16_t*)&check = 0;
        for(; len > 1; len -= 2)
            sum += *(buf++);
        len = DataLen;
        if(Data == nullptr)
        {
            for(; len > 1; len -= 2)
                sum += *(buf++);
        } else
        {
            if(len == 1)
                sum += *((uint8_t *)(buf++));
            buf = (uint16_t*)Data;
            for(; len > 1; len -= 2)
                sum += *(buf++);
        }
        if(len == 1)
            sum += *((uint8_t *)buf);
        sum += htons(sizeof(udphdr) + DataLen);
        while(sum >> 16)
            sum = (sum & 0xFFFF) + (sum >> 16);
        *(uint16_t*)&check = (uint16_t)(~sum);
    }
};

struct icmphdr
{
    uint8_t type;
    uint8_t code;
    LqByteOrderNum<uint16_t, true>   check;

    struct
    {
        LqByteOrderNum<uint16_t, true> id;
        LqByteOrderNum<uint16_t, true> seq;
        char data[1];
    } echo;

    void ComputeChecksum(size_t CommonLen)
    {
        *(uint16_t*)&check = 0;
        int sum = 0;
        size_t count = CommonLen;
        uint16_t* addr = (uint16_t*)this;
        for(sum = 0; count > 1; count -= 2)
            sum += *addr++;
        if(count == 1)
            sum += (char)*addr;
        sum = (sum >> 16) + (sum & 0xFFFF);
        sum += (sum >> 16);
        *(uint16_t*)&check = ~sum;
    }
};

#pragma pack(pop)