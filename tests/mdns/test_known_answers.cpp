// Known answers in a query are read only when the query code uses them.
//
// A query can list answers the asker already has ("known answers"): a browse
// for a service type lists every device of that type. The core's code read each
// one whole (one `new` of about 540 B) and freed them after the packet. A
// failed `new` resets the ESP8266. The library copy in lib/ESP8266mDNS reads a
// known answer whole only when the loop in _parseQuery acts on it (port of
// esphome-libs/arduino-esp8266 #9 by bdraco, who wrote the first version of
// this test as a host test of the core's emulator).
//
// The library needs lwIP and the Arduino core, so it cannot be built here.
// tests/mdns/extract_functions.py cuts the REAL text of _udpReadDomainIs,
// _knownPTRTargetIsOurs, _readKnownAnswer and _replyMaskForService out of
// LEAmDNS_Control.cpp, and this file compiles that text against a small fake of
// the class: a byte buffer for the packet, a reader for names with compression
// pointers, and an answer type that counts how many are made.
// Fake, not the library: the packet reader (_readRRHeader, _readRRAnswer), the
// service and host data, the domain builders and _replyMaskForHost. What the
// test cannot see is the known answer loop itself and the real packet reader;
// the device run covers them.
//
// The packets are built like bdraco's cases: 20 known answers of other
// instances (alternately with a compressed and a spelled-out target), ours
// among them or not, too little TTL, a type we do not offer, SRV queries.
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <strings.h>
#include <vector>

#define DEBUG_EX_INFO(...)
#define DEBUG_EX_ERR(...)

#define MDNS_DOMAIN_LABEL_MAXLENGTH 63
#define MDNS_DOMAIN_COMPRESS_MARK 0xC0
#define MDNS_DOMAIN_MAX_REDIRCTION 6
#define MDNS_HOST_TTL 120
#define MDNS_SERVICE_TTL 4500
#define MDNS_DOMAIN_MAXLENGTH 256

#define DNS_RRTYPE_A 1
#define DNS_RRTYPE_PTR 12
#define DNS_RRTYPE_TXT 16
#define DNS_RRTYPE_SRV 33
#define DNS_RRTYPE_ANY 255
#define DNS_RRCLASS_IN 1
#define DNS_RRCLASS_ANY 255

static int g_fail = 0;
#define CHECK(c)                                                              \
    do {                                                                      \
        if (!(c)) {                                                           \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c);          \
            ++g_fail;                                                         \
        }                                                                     \
    } while (0)
#define CHECK_EQ(a, b)                                                        \
    do {                                                                      \
        long long va = (a), vb = (b);                                         \
        if (va != vb) {                                                       \
            std::printf("FAIL %s:%d  %s == %s  (%lld vs %lld)\n", __FILE__,   \
                        __LINE__, #a, #b, va, vb);                            \
            ++g_fail;                                                         \
        }                                                                     \
    } while (0)

namespace esp8266 {
namespace MDNSImplementation {

// The fake of the class, with the names and shapes the extracted code uses.
// Content flags have the values of LEAmDNS.h.
class MDNSResponder {
public:
    enum ContentFlags : uint8_t {
        ContentFlag_A        = 0x01,
        ContentFlag_PTR_IP4  = 0x02,
        ContentFlag_PTR_IP6  = 0x04,
        ContentFlag_AAAA     = 0x08,
        ContentFlag_PTR_TYPE = 0x10,
        ContentFlag_PTR_NAME = 0x20,
        ContentFlag_TXT      = 0x40,
        ContentFlag_SRV      = 0x80,
    };

    // Wire format, as the library keeps it: length-prefixed labels, a 0 at
    // the end, m_u16NameLength counting that 0.
    struct stcMDNS_RRDomain {
        char     m_acName[MDNS_DOMAIN_MAXLENGTH];
        uint16_t m_u16NameLength = 0;
        void clear() { m_u16NameLength = 0; }
        void add(const char* label)
        {
            size_t n = std::strlen(label);
            if (m_u16NameLength)
                --m_u16NameLength;  // overwrite the closing 0
            m_acName[m_u16NameLength++] = (char)n;
            std::memcpy(&m_acName[m_u16NameLength], label, n);
            m_u16NameLength += (uint16_t)n;
            m_acName[m_u16NameLength++] = 0;
        }
        bool operator==(const stcMDNS_RRDomain& o) const
        {
            return (m_u16NameLength == o.m_u16NameLength)
                   && (0 == strncasecmp(m_acName, o.m_acName, m_u16NameLength));
        }
    };
    struct stcMDNS_RRAttributes {
        uint16_t m_u16Type  = 0;
        uint16_t m_u16Class = 0;
    };
    struct stcMDNS_RRHeader {
        stcMDNS_RRDomain     m_Domain;
        stcMDNS_RRAttributes m_Attributes;
    };
    struct stcMDNS_RRAnswer {
        static int      made;
        stcMDNS_RRHeader m_Header;
        uint32_t         m_u32TTL = 0;
        stcMDNS_RRAnswer() { ++made; }
    };
    struct stcMDNSSendParameter {
        uint8_t m_u8HostReplyMask = 0;
    };
    struct stcProbeInformation {
        bool m_bTiebreakNeeded = false;
    };
    struct stcMDNSService {
        stcMDNSService*     m_pNext      = nullptr;
        uint8_t             m_u8ReplyMask = 0;
        stcProbeInformation m_ProbeInformation;
        const char*         m_pcName     = "katest";
        const char*         m_pcService  = "http";
        const char*         m_pcProtocol = "tcp";
    };

    // The packet, with the read position of the core's UdpContext.
    struct UdpContext {
        std::vector<uint8_t> data;
        size_t               pos = 0;
        size_t tell() const { return pos; }
        void   seek(size_t p) { pos = p; }
        bool   isValidOffset(size_t p) const { return p <= data.size(); }
    };

    UdpContext*         m_pUDPContext;
    stcMDNSService*     m_pServices = nullptr;
    stcProbeInformation m_HostProbeInformation;
    const char*         m_pcHostname = "katest";

    // The code under test (the real text; declared as in LEAmDNS.h)
    bool _readKnownAnswer(const stcMDNSSendParameter& p_SendParameter,
                          stcMDNS_RRAnswer*&          p_rpRRAnswer);
    bool _knownPTRTargetIsOurs(const stcMDNSService& p_Service, uint8_t p_u8ReplyMask,
                               size_t p_stRData);
    bool _udpReadDomainIs(const stcMDNS_RRDomain& p_Domain);
    uint8_t _replyMaskForService(const stcMDNS_RRHeader& p_RRHeader,
                                 const stcMDNSService& p_Service, bool* p_pbFullNameMatch = 0) const;

    // Fakes
    bool _udpRead8(uint8_t& v)
    {
        if (m_pUDPContext->pos >= m_pUDPContext->data.size())
            return false;
        v = m_pUDPContext->data[m_pUDPContext->pos++];
        return true;
    }
    bool _udpRead16(uint16_t& v)
    {
        uint8_t a, b;
        if (!(_udpRead8(a) && _udpRead8(b)))
            return false;
        v = (uint16_t)((a << 8) | b);
        return true;
    }
    bool _udpRead32(uint32_t& v)
    {
        uint16_t a, b;
        if (!(_udpRead16(a) && _udpRead16(b)))
            return false;
        v = ((uint32_t)a << 16) | b;
        return true;
    }
    bool _udpReadBuffer(unsigned char* p, size_t n)
    {
        for (size_t i = 0; i < n; ++i)
            if (!_udpRead8(p[i]))
                return false;
        return true;
    }
    // A name at the read position, compression pointers followed; the read
    // position ends after the name (or after its first pointer).
    bool readDomain(stcMDNS_RRDomain& d)
    {
        d.clear();
        size_t resume    = 0;
        int    redirects = 0;
        for (;;) {
            uint8_t len;
            if (!_udpRead8(len))
                return false;
            if (len & MDNS_DOMAIN_COMPRESS_MARK) {
                uint8_t low;
                if (!_udpRead8(low) || (MDNS_DOMAIN_MAX_REDIRCTION < ++redirects))
                    return false;
                size_t off = (((size_t)(len & ~MDNS_DOMAIN_COMPRESS_MARK)) << 8) | low;
                if (!m_pUDPContext->isValidOffset(off))
                    return false;
                if (!resume)
                    resume = m_pUDPContext->pos;
                m_pUDPContext->seek(off);
                continue;
            }
            if (0 == len) {
                if (d.m_u16NameLength)
                    --d.m_u16NameLength;
                d.m_acName[d.m_u16NameLength++] = 0;
                break;
            }
            char label[MDNS_DOMAIN_LABEL_MAXLENGTH + 1];
            if (!_udpReadBuffer((unsigned char*)label, len))
                return false;
            label[len] = 0;
            d.add(label);
        }
        if (resume)
            m_pUDPContext->seek(resume);
        return true;
    }
    bool _readRRHeader(stcMDNS_RRHeader& h)
    {
        return readDomain(h.m_Domain) && _udpRead16(h.m_Attributes.m_u16Type)
               && _udpRead16(h.m_Attributes.m_u16Class);
    }
    // Reads one whole record, as the library's _readRRAnswer does; counts it.
    bool _readRRAnswer(stcMDNS_RRAnswer*& a)
    {
        a = new stcMDNS_RRAnswer;
        uint16_t rdlen;
        if (!(_readRRHeader(a->m_Header) && _udpRead32(a->m_u32TTL) && _udpRead16(rdlen)))
            return false;
        m_pUDPContext->seek(m_pUDPContext->pos + rdlen);
        return m_pUDPContext->isValidOffset(m_pUDPContext->pos);
    }
    bool _buildDomainForService(const stcMDNSService& s, bool withName, stcMDNS_RRDomain& d) const
    {
        d.clear();
        if (withName)
            d.add(s.m_pcName);
        std::string svc = std::string("_") + s.m_pcService;
        std::string pro = std::string("_") + s.m_pcProtocol;
        d.add(svc.c_str());
        d.add(pro.c_str());
        d.add("local");
        return true;
    }
    bool _buildDomainForDNSSD(stcMDNS_RRDomain& d) const
    {
        d.clear();
        d.add("_services");
        d.add("_dns-sd");
        d.add("_udp");
        d.add("local");
        return true;
    }
    // The host's A record (the library's version also looks at reverse names).
    uint8_t _replyMaskForHost(const stcMDNS_RRHeader& h) const
    {
        stcMDNS_RRDomain host;
        host.add(m_pcHostname);
        host.add("local");
        if (((DNS_RRCLASS_IN == h.m_Attributes.m_u16Class)
             || (DNS_RRCLASS_ANY == h.m_Attributes.m_u16Class))
            && (h.m_Domain == host)
            && ((DNS_RRTYPE_A == h.m_Attributes.m_u16Type)
                || (DNS_RRTYPE_ANY == h.m_Attributes.m_u16Type)))
            return ContentFlag_A;
        return 0;
    }
};
int MDNSResponder::stcMDNS_RRAnswer::made = 0;

}  // namespace MDNSImplementation
}  // namespace esp8266

using esp8266::MDNSImplementation::MDNSResponder;

namespace esp8266 {
namespace MDNSImplementation {
// The real text, cut out of the library by tests/mdns/extract_functions.py
#include KNOWN_ANSWER_FUNCTIONS
}  // namespace MDNSImplementation
}  // namespace esp8266

// ---- packets, built like bdraco's host test ----

struct Packet {
    std::vector<uint8_t> d;
    void u8(uint8_t v) { d.push_back(v); }
    void u16(uint16_t v)
    {
        u8((uint8_t)(v >> 8));
        u8((uint8_t)(v & 0xff));
    }
    void u32(uint32_t v)
    {
        u16((uint16_t)(v >> 16));
        u16((uint16_t)(v & 0xffff));
    }
    void label(const char* s)
    {
        u8((uint8_t)std::strlen(s));
        for (const char* c = s; *c; ++c)
            u8((uint8_t)*c);
    }
    void pointer(uint16_t off) { u16((uint16_t)(0xc000 | off)); }
    size_t len() const { return d.size(); }
};

enum class Ours { NONE, TTL_LONG, TTL_SHORT, CACHE_FLUSH };

struct Query {
    Packet              p;
    std::vector<size_t> ends;      // where each known answer ends
    std::vector<bool>   isOurs;    // which known answers are ours
    size_t              firstAnswer = 0;
    size_t              ourQuestion = 0;  // where our question starts
};

static const unsigned OTHERS = 20;

// Two questions (ours, then an unrelated one), then the known answers.
// service: queried type ("_http" is ours; the library keeps it as "http").
// srv: ask for our instance's SRV instead of browsing the type.
static void buildQuery(Query& q, const char* service, bool srv, Ours ours)
{
    Packet& p = q.p;
    p.u16(0);
    p.u16(0);
    p.u16(2);
    p.u16((uint16_t)(OTHERS + (Ours::NONE != ours ? 1 : 0)));
    p.u16(0);
    p.u16(0);
    q.ourQuestion = p.len();
    const uint16_t instanceOffset = (uint16_t)p.len();
    if (srv)
        p.label("katest");
    const uint16_t serviceOffset = (uint16_t)p.len();
    p.label(service);
    const uint16_t tcpOffset = (uint16_t)p.len();
    p.label("_tcp");
    p.label("local");
    p.u8(0);
    p.u16(srv ? 33 : 12);
    p.u16(0x8001);  // unicast reply wanted
    p.label("_other");
    p.pointer(tcpOffset);
    p.u16(12);
    p.u16(1);
    q.firstAnswer = p.len();

    auto ptrAnswer = [&](const char* inst, uint16_t cls, uint32_t ttl, bool compressed, bool mine) {
        p.pointer(serviceOffset);
        p.u16(12);
        p.u16(cls);
        p.u32(ttl);
        size_t rd = p.len();
        p.u16(0);
        p.label(inst);
        if (compressed) {
            p.pointer(serviceOffset);
        } else {
            p.label(service);
            p.label("_tcp");
            p.label("local");
            p.u8(0);
        }
        uint16_t n = (uint16_t)(p.len() - rd - 2);
        p.d[rd]     = (uint8_t)(n >> 8);
        p.d[rd + 1] = (uint8_t)(n & 0xff);
        q.ends.push_back(p.len());
        q.isOurs.push_back(mine);
    };
    auto srvAnswer = [&](const char* inst, bool mine) {
        if (mine) {
            p.pointer(instanceOffset);
        } else {
            p.label(inst);
            p.pointer(serviceOffset);
        }
        p.u16(33);
        p.u16(1);
        p.u32(4500);
        p.u16((uint16_t)(6 + 1 + std::strlen(inst) + 1 + 5 + 1));
        p.u16(0);
        p.u16(0);
        p.u16(80);
        p.label(inst);
        p.label("local");
        p.u8(0);
        q.ends.push_back(p.len());
        q.isOurs.push_back(mine);
    };
    char other[16];
    for (unsigned i = 0; i < OTHERS; ++i) {
        std::snprintf(other, sizeof(other), "dev%u", i);
        if (srv)
            srvAnswer(other, false);
        else
            ptrAnswer(other, 1, 4500, (0 == (i % 2)), false);
        if ((OTHERS / 2 == i) && (Ours::NONE != ours)) {
            if (srv)
                srvAnswer("katest", true);
            else
                ptrAnswer("katest", (Ours::CACHE_FLUSH == ours) ? 0x8001 : 1,
                          (Ours::TTL_SHORT == ours) ? 100 : 4500, true, true);
        }
    }
}

// What _parseQuery does around the loop: planned replies come from the
// question, then every known answer goes through _readKnownAnswer.
struct Run {
    std::vector<bool> read;      // per known answer: was it read whole
    std::vector<bool> posOk;     // per known answer: position after the call
    bool              allOk = true;
    int               made  = 0;
};

static Run runQuery(const Query& q, MDNSResponder::stcMDNSService& service, bool tiebreakHost,
                    uint8_t hostMask)
{
    MDNSResponder::UdpContext udp;
    udp.data = q.p.d;
    MDNSResponder r;
    r.m_pUDPContext = &udp;
    service.m_pNext = nullptr;
    r.m_pServices   = &service;
    r.m_HostProbeInformation.m_bTiebreakNeeded = tiebreakHost;

    // The planned reply, from the first question (the second is not ours)
    udp.seek(q.ourQuestion);
    MDNSResponder::stcMDNS_RRHeader question;
    r._readRRHeader(question);
    question.m_Attributes.m_u16Class &= (uint16_t)~0x8000;
    service.m_u8ReplyMask = r._replyMaskForService(question, service);
    MDNSResponder::stcMDNSSendParameter sp;
    sp.m_u8HostReplyMask = hostMask;

    udp.seek(q.firstAnswer);
    MDNSResponder::stcMDNS_RRAnswer::made = 0;
    Run out;
    for (size_t i = 0; i < q.ends.size(); ++i) {
        MDNSResponder::stcMDNS_RRAnswer* a = nullptr;
        bool ok = r._readKnownAnswer(sp, a);
        out.allOk = out.allOk && ok;
        out.read.push_back(a != nullptr);
        out.posOk.push_back(udp.tell() == q.ends[i]);
        delete a;
    }
    out.made = MDNSResponder::stcMDNS_RRAnswer::made;
    return out;
}

static unsigned count(const Run& r)
{
    unsigned n = 0;
    for (size_t i = 0; i < r.read.size(); ++i)
        n += r.read[i] ? 1 : 0;
    return n;
}

static bool onlyOursRead(const Query& q, const Run& r)
{
    for (size_t i = 0; i < q.isOurs.size(); ++i)
        if (r.read[i] != (bool)q.isOurs[i])
            return false;
    return true;
}

static bool positionsOk(const Run& r)
{
    for (size_t i = 0; i < r.posOk.size(); ++i)
        if (!r.posOk[i])
            return false;
    return true;
}

static void browse(const char* name, const char* service, bool srv, Ours ours, unsigned expected)
{
    Query q;
    buildQuery(q, service, srv, ours);
    MDNSResponder::stcMDNSService svc;
    Run r = runQuery(q, svc, false, 0);
    std::printf("%s: %u read whole (expected %u), %d objects made\n", name, count(r), expected,
                r.made);
    CHECK(r.allOk);
    CHECK_EQ(count(r), expected);
    CHECK_EQ(r.made, (int)expected);  // the count of answer objects, the heap cost
    CHECK(positionsOk(r));            // the next answer starts where it should
    if (expected)
        CHECK(onlyOursRead(q, r));    // and the one read is ours
}

static void test_cases_of_the_host_test()
{
    browse("other instances only", "_http", false, Ours::NONE, 0);
    browse("ours among others", "_http", false, Ours::TTL_LONG, 1);
    browse("ours with the cache-flush bit", "_http", false, Ours::CACHE_FLUSH, 1);
    browse("ours with too little TTL left", "_http", false, Ours::TTL_SHORT, 0);
    browse("a type we do not offer", "_none", false, Ours::NONE, 0);
    browse("SRV, other instances only", "_http", true, Ours::NONE, 0);
    browse("SRV, ours among others", "_http", true, Ours::TTL_LONG, 1);
}

// A DNS-SD enumeration (_services._dns-sd._udp.local) plans a PTR to our
// service TYPE; its known answers name other types, ours among them.
static void test_service_enumeration()
{
    Query q;
    Packet& p = q.p;
    p.u16(0);
    p.u16(0);
    p.u16(1);
    p.u16(11);
    p.u16(0);
    p.u16(0);
    q.ourQuestion = p.len();
    p.label("_services");
    p.label("_dns-sd");
    p.label("_udp");
    p.label("local");
    p.u8(0);
    p.u16(12);
    p.u16(0x8001);
    q.firstAnswer = p.len();
    for (int i = 0; i < 11; ++i) {
        p.pointer((uint16_t)q.ourQuestion);
        p.u16(12);
        p.u16(1);
        p.u32(4500);
        const bool mine = (i == 5);
        char type[16];
        std::snprintf(type, sizeof(type), "_type%d", i);
        p.u16((uint16_t)(1 + std::strlen(mine ? "_http" : type) + 5 + 6 + 1));  // labels + root
        p.label(mine ? "_http" : type);
        p.label("_tcp");
        p.label("local");
        p.u8(0);
        q.ends.push_back(p.len());
        q.isOurs.push_back(mine);
    }
    MDNSResponder::stcMDNSService svc;
    Run r = runQuery(q, svc, false, 0);
    CHECK(r.allOk);
    CHECK_EQ(count(r), 1);
    CHECK(onlyOursRead(q, r));
    CHECK(positionsOk(r));
}

// With nothing planned, a known answer is never used, whoever it names.
static void test_no_planned_reply_reads_nothing()
{
    Query q;
    buildQuery(q, "_http", false, Ours::TTL_LONG);
    MDNSResponder::stcMDNSService svc;
    MDNSResponder::UdpContext     udp;
    udp.data = q.p.d;
    MDNSResponder r;
    r.m_pUDPContext = &udp;
    r.m_pServices   = &svc;
    svc.m_u8ReplyMask = 0;
    MDNSResponder::stcMDNSSendParameter sp;
    udp.seek(q.firstAnswer);
    MDNSResponder::stcMDNS_RRAnswer::made = 0;
    for (size_t i = 0; i < q.ends.size(); ++i) {
        MDNSResponder::stcMDNS_RRAnswer* a = nullptr;
        CHECK(r._readKnownAnswer(sp, a));
        CHECK(a == nullptr);
        CHECK_EQ(udp.tell(), q.ends[i]);
    }
    CHECK_EQ(MDNSResponder::stcMDNS_RRAnswer::made, 0);
}

// A pending tiebreak needs every answer, so all are read (but not ANY ones).
static void test_tiebreak_reads_all()
{
    Query q;
    buildQuery(q, "_http", false, Ours::NONE);
    MDNSResponder::stcMDNSService svc;
    Run r = runQuery(q, svc, true, 0);
    CHECK(r.allOk);
    CHECK_EQ(count(r), OTHERS);
    CHECK(positionsOk(r));

    MDNSResponder::stcMDNSService svc2;
    svc2.m_ProbeInformation.m_bTiebreakNeeded = true;
    Run r2 = runQuery(q, svc2, false, 0);
    CHECK_EQ(count(r2), OTHERS);
}

// A planned host reply (an A query for our host name) uses our own A record
// with enough TTL, and no other host's.
static void test_host_answer()
{
    for (int variant = 0; variant < 3; ++variant) {
        Packet p;
        p.u16(0);
        p.u16(0);
        p.u16(1);
        p.u16(3);
        p.u16(0);
        p.u16(0);
        size_t qStart = p.len();
        p.label("katest");
        p.label("local");
        p.u8(0);
        p.u16(1);
        p.u16(1);
        size_t first = p.len();
        auto a = [&](const char* host, uint32_t ttl) {
            p.label(host);
            p.label("local");
            p.u8(0);
            p.u16(1);
            p.u16(1);
            p.u32(ttl);
            p.u16(4);
            p.u32(0xc6336401);  // 198.51.100.1
        };
        a("other", 120);
        a("katest", variant == 1 ? 30 : 120);  // variant 1: too little TTL
        a(variant == 2 ? "katest" : "third", 120);
        (void)qStart;
        MDNSResponder::UdpContext udp;
        udp.data = p.d;
        MDNSResponder r;
        r.m_pUDPContext = &udp;
        MDNSResponder::stcMDNSService* none = nullptr;
        r.m_pServices = none;
        MDNSResponder::stcMDNSSendParameter sp;
        sp.m_u8HostReplyMask = MDNSResponder::ContentFlag_A;
        udp.seek(first);
        MDNSResponder::stcMDNS_RRAnswer::made = 0;
        unsigned n = 0;
        for (int i = 0; i < 3; ++i) {
            MDNSResponder::stcMDNS_RRAnswer* ans = nullptr;
            CHECK(r._readKnownAnswer(sp, ans));
            n += ans ? 1 : 0;
            delete ans;
        }
        CHECK_EQ(udp.tell(), p.len());
        CHECK_EQ(n, variant == 0 ? 1 : (variant == 1 ? 0 : 2));
    }
}

// An answer of type ANY or class ANY is never used by the loop, so never read.
static void test_any_answers_are_skipped()
{
    Packet p;
    p.u16(0);
    p.u16(0);
    p.u16(0);
    p.u16(2);
    p.u16(0);
    p.u16(0);
    size_t first = p.len();
    for (int i = 0; i < 2; ++i) {
        p.label("katest");
        p.label("local");
        p.u8(0);
        p.u16(i == 0 ? 255 : 1);
        p.u16(i == 0 ? 1 : 255);
        p.u32(120);
        p.u16(4);
        p.u32(0xc6336401);
    }
    MDNSResponder::UdpContext udp;
    udp.data = p.d;
    MDNSResponder r;
    r.m_pUDPContext = &udp;
    r.m_HostProbeInformation.m_bTiebreakNeeded = true;
    MDNSResponder::stcMDNSSendParameter sp;
    sp.m_u8HostReplyMask = MDNSResponder::ContentFlag_A;
    udp.seek(first);
    MDNSResponder::stcMDNS_RRAnswer::made = 0;
    for (int i = 0; i < 2; ++i) {
        MDNSResponder::stcMDNS_RRAnswer* ans = nullptr;
        CHECK(r._readKnownAnswer(sp, ans));
        CHECK(ans == nullptr);
    }
    CHECK_EQ(MDNSResponder::stcMDNS_RRAnswer::made, 0);
    CHECK_EQ(udp.tell(), p.len());
}

// A record that does not fit the packet fails the read, as before.
static void test_truncated_known_answer_fails()
{
    Query q;
    buildQuery(q, "_http", false, Ours::NONE);
    const size_t firstEnd = q.ends[0];
    // data length too long for the packet
    {
        Query t = q;
        // the first answer's RDLENGTH sits 2 bytes before its data; its name
        // is a 2 byte pointer, then type, class, TTL (8 bytes), then RDLENGTH
        size_t rd = t.firstAnswer + 2 + 2 + 2 + 4;
        t.p.d[rd]     = 0x7f;
        t.p.d[rd + 1] = 0xff;
        MDNSResponder::stcMDNSService svc;
        MDNSResponder::UdpContext     udp;
        udp.data = t.p.d;
        MDNSResponder r;
        r.m_pUDPContext = &udp;
        r.m_pServices   = &svc;
        svc.m_u8ReplyMask = MDNSResponder::ContentFlag_PTR_NAME;
        MDNSResponder::stcMDNSSendParameter sp;
        udp.seek(t.firstAnswer);
        MDNSResponder::stcMDNS_RRAnswer* a = nullptr;
        CHECK(!r._readKnownAnswer(sp, a));
        delete a;
    }
    // the packet ends inside the first header
    {
        Query t = q;
        t.p.d.resize(t.firstAnswer + 5);
        MDNSResponder::stcMDNSService svc;
        MDNSResponder::UdpContext     udp;
        udp.data = t.p.d;
        MDNSResponder r;
        r.m_pUDPContext = &udp;
        r.m_pServices   = &svc;
        MDNSResponder::stcMDNSSendParameter sp;
        udp.seek(t.firstAnswer);
        MDNSResponder::stcMDNS_RRAnswer* a = nullptr;
        CHECK(!r._readKnownAnswer(sp, a));
        delete a;
    }
    (void)firstEnd;
}

// _udpReadDomainIs: whole-name comparison with compression pointers
static void test_domain_compare()
{
    Packet p;
    p.label("_http");  // offset 0
    p.label("_tcp");
    p.label("local");
    p.u8(0);
    size_t a = p.len();  // Katest + pointer to 0
    p.label("Katest");
    p.pointer(0);
    size_t b = p.len();  // katest.other (not ours)
    p.label("katest");
    p.label("other");
    p.u8(0);
    size_t c = p.len();  // a pointer loop
    p.pointer((uint16_t)c);
    size_t d = p.len();  // longer than ours: katest._http._tcp.local.extra
    p.label("katest");
    p.label("_http");
    p.label("_tcp");
    p.label("local");
    p.label("extra");
    p.u8(0);
    MDNSResponder::UdpContext udp;
    udp.data = p.d;
    MDNSResponder r;
    r.m_pUDPContext = &udp;
    MDNSResponder::stcMDNSService svc;
    MDNSResponder::stcMDNS_RRDomain ours;
    r._buildDomainForService(svc, true, ours);
    udp.seek(a);
    CHECK(r._udpReadDomainIs(ours));
    udp.seek(b);
    CHECK(!r._udpReadDomainIs(ours));
    udp.seek(c);
    CHECK(!r._udpReadDomainIs(ours));
    udp.seek(d);
    CHECK(!r._udpReadDomainIs(ours));
}

// _parseQuery must go through _readKnownAnswer. The function needs lwIP, so
// its source is checked, the body only.
static std::string readFile(const char* path)
{
    std::ifstream     f(path);
    std::stringstream s;
    s << f.rdbuf();
    return s.str();
}

static void test_parse_query_uses_the_gate()
{
    std::string src = readFile("lib/ESP8266mDNS/src/LEAmDNS_Control.cpp");
    size_t      b   = src.find("bool MDNSResponder::_parseQuery(");
    size_t      e   = src.find("MDNSResponder::_parseResponse", b);
    CHECK(b != std::string::npos);
    CHECK(e != std::string::npos);
    std::string body = src.substr(b, e - b);
    CHECK(body.find("_readKnownAnswer(sendParameter, pKnownRRAnswer)") != std::string::npos);
    // no known answer is read whole by the old call
    CHECK(body.find("_readRRAnswer(pKnownRRAnswer)") == std::string::npos);
    // A skipped answer (success, no object) is not an error
    size_t failed = body.find("FAILED to read known answer");
    size_t elseIf = body.rfind("else if (!bResult)", failed);
    CHECK(failed != std::string::npos);
    CHECK(elseIf != std::string::npos);

    // The class declares the three functions (the firmware build needs them;
    // this test compiles against a fake class and would not notice).
    std::string hdr = readFile("lib/ESP8266mDNS/src/LEAmDNS.h");
    CHECK(hdr.find("bool _readKnownAnswer(const stcMDNSSendParameter& p_SendParameter,")
          != std::string::npos);
    CHECK(hdr.find("bool _knownPTRTargetIsOurs(const stcMDNSService& p_Service, uint8_t p_u8ReplyMask,")
          != std::string::npos);
    CHECK(hdr.find("bool _udpReadDomainIs(const stcMDNS_RRDomain& p_Domain);") != std::string::npos);
}

int main()
{
    test_cases_of_the_host_test();
    test_service_enumeration();
    test_no_planned_reply_reads_nothing();
    test_tiebreak_reads_all();
    test_host_answer();
    test_any_answers_are_skipped();
    test_truncated_known_answer_fails();
    test_domain_compare();
    test_parse_query_uses_the_gate();
    if (g_fail) {
        std::printf("mdns known answer tests: %d FAILED\n", g_fail);
        return 1;
    }
    std::printf("mdns known answer tests: ALL PASSED\n");
    return 0;
}
