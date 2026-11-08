// A response from another device is handled one record at a time.
//
// The library copy in lib/ESP8266mDNS reads the records of a response that
// arrives while the device probes its own name. The core's code kept all of
// them alive until the end of the packet (one `new` of 288-540 B each; a failed
// `new` resets the ESP8266). Here the record is a counting fake, the reader
// serves a crafted packet, and the test reads the peak number of live records.
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "LEAmDNS_Stream.h"

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

struct FakeAnswer {
    static int  live, peak, made;
    FakeAnswer* next;
    int         id;
    explicit FakeAnswer(int i) : next(nullptr), id(i)
    {
        ++made;
        if (++live > peak)
            peak = live;
    }
    ~FakeAnswer() { --live; }
};
int FakeAnswer::live = 0, FakeAnswer::peak = 0, FakeAnswer::made = 0;

static void reset()
{
    FakeAnswer::live = FakeAnswer::peak = FakeAnswer::made = 0;
}

// A packet of `count` records. Reading record `failRead` fails (-1: never);
// `nullAt` makes a read report success but give no record.
struct Packet {
    int              count    = 0;
    int              failRead = -1;
    int              nullAt   = -1;
    int              reads    = 0;
    std::vector<int> handled;
};

static bool readOne(Packet& p, FakeAnswer*& out)
{
    int idx = p.reads++;
    if (idx == p.failRead)
        return false;
    if (idx == p.nullAt) {
        out = nullptr;
        return true;
    }
    out = new FakeAnswer(idx);
    return true;
}

static void test_many_records_peak_is_one()
{
    reset();
    Packet p;
    p.count = 64;
    bool ok = mdnsHandleAnswersOneByOne<FakeAnswer>(
        p.count, [&](FakeAnswer*& a) { return readOne(p, a); },
        [&](const FakeAnswer& a) {
            p.handled.push_back(a.id);
            return true;
        });
    CHECK(ok);
    CHECK_EQ(FakeAnswer::made, 64);
    CHECK_EQ(FakeAnswer::peak, 1);  // the property that matters
    CHECK_EQ(FakeAnswer::live, 0);
    CHECK_EQ((long long)p.handled.size(), 64);
    bool inOrder = true;
    for (size_t i = 0; i < p.handled.size(); ++i)
        inOrder = inOrder && p.handled[i] == (int)i;
    CHECK(inOrder);
}

static void test_record_alive_while_handled()
{
    reset();
    Packet p;
    p.count           = 3;
    int liveInHandler = -1;
    mdnsHandleAnswersOneByOne<FakeAnswer>(
        p.count, [&](FakeAnswer*& a) { return readOne(p, a); },
        [&](const FakeAnswer&) {
            liveInHandler = FakeAnswer::live;
            return true;
        });
    CHECK_EQ(liveInHandler, 1);
}

static void test_no_records()
{
    reset();
    Packet p;
    bool   ok = mdnsHandleAnswersOneByOne<FakeAnswer>(
        0, [&](FakeAnswer*& a) { return readOne(p, a); },
        [&](const FakeAnswer&) { return true; });
    CHECK(ok);
    CHECK_EQ(p.reads, 0);
    CHECK_EQ(FakeAnswer::made, 0);
}

static void test_handler_failure_stops_and_frees()
{
    reset();
    Packet p;
    p.count = 10;
    bool ok = mdnsHandleAnswersOneByOne<FakeAnswer>(
        p.count, [&](FakeAnswer*& a) { return readOne(p, a); },
        [&](const FakeAnswer& a) {
            p.handled.push_back(a.id);
            return a.id != 4;
        });
    CHECK(!ok);
    CHECK_EQ(p.reads, 5);  // nothing read after the failing record
    CHECK_EQ(FakeAnswer::live, 0);
    CHECK_EQ(FakeAnswer::peak, 1);
}

static void test_read_failure_stops_and_frees()
{
    reset();
    Packet p;
    p.count    = 10;
    p.failRead = 3;
    bool ok = mdnsHandleAnswersOneByOne<FakeAnswer>(
        p.count, [&](FakeAnswer*& a) { return readOne(p, a); },
        [&](const FakeAnswer& a) {
            p.handled.push_back(a.id);
            return true;
        });
    CHECK(!ok);
    CHECK_EQ(p.reads, 4);
    CHECK_EQ((long long)p.handled.size(), 3);  // the records before it were handled
    CHECK_EQ(FakeAnswer::live, 0);
}

static void test_success_without_record_is_a_failure()
{
    reset();
    Packet p;
    p.count  = 5;
    p.nullAt = 2;
    bool ok  = mdnsHandleAnswersOneByOne<FakeAnswer>(
        p.count, [&](FakeAnswer*& a) { return readOne(p, a); },
        [&](const FakeAnswer& a) {
            p.handled.push_back(a.id);
            return true;
        });
    CHECK(!ok);
    CHECK_EQ(p.reads, 3);
    CHECK_EQ(FakeAnswer::live, 0);
}

// _parseResponse must use the loop. Checked on the source, because the
// function needs lwIP. Looks at the function body only.
static std::string readFile(const char* path)
{
    std::ifstream     f(path);
    std::stringstream s;
    s << f.rdbuf();
    return s.str();
}

static void test_parse_response_uses_the_loop()
{
    std::string src = readFile("lib/ESP8266mDNS/src/LEAmDNS_Control.cpp");
    size_t      b   = src.find("bool MDNSResponder::_parseResponse(");
    size_t      e   = src.find("MDNSResponder::_processAnswers", b);
    CHECK(b != std::string::npos);
    CHECK(e != std::string::npos);
    std::string body  = src.substr(b, e - b);
    size_t      guard = body.find("bStream = !_hasServiceQueriesWaitingForAnswers()");
    size_t      use   = body.find("mdnsHandleAnswersOneByOne<");
    size_t      old   = body.find("_readRRAnswer(pRRAnswer)");
    CHECK(guard != std::string::npos);
    CHECK(use != std::string::npos);
    CHECK(old != std::string::npos);  // kept for the service query case
    // Each record is handled on its own, as a list of one.
    CHECK(body.find("_processAnswers(&rRAnswer)") != std::string::npos);
    // The streaming call comes after the "queries waiting" test, and the
    // collecting code after it (the branch for waiting service queries).
    CHECK(body.find("if (bStream)") != std::string::npos);
    // The collecting loop must not run as well, or every record is read twice.
    CHECK(body.find("((!bStream) && (bResult)") != std::string::npos);
    CHECK(guard < use);
    CHECK(use < old);
    CHECK(src.find("#include \"LEAmDNS_Stream.h\"") != std::string::npos);
}

int main()
{
    test_many_records_peak_is_one();
    test_record_alive_while_handled();
    test_no_records();
    test_handler_failure_stops_and_frees();
    test_read_failure_stops_and_frees();
    test_success_without_record_is_a_failure();
    test_parse_response_uses_the_loop();
    if (g_fail) {
        std::printf("mdns answer stream tests: %d FAILED\n", g_fail);
        return 1;
    }
    std::printf("mdns answer stream tests: ALL PASSED\n");
    return 0;
}
