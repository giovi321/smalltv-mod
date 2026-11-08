/*
    LEAmDNS_Stream.h

    Added for smalltv-mod; not part of the ESP8266 Arduino core's library.
    License: MIT, as the rest of this library (see the header of
    LEAmDNS_Control.cpp).

    Handle the answer records of a response one by one: read a record, hand it
    to `handle`, free it, then read the next. At most one record is alive at a
    time. The core's code read all records into a list first (one `new` of 288
    to about 540 bytes each) and freed them after the whole packet; a failed
    `new` resets the ESP8266.

    No Arduino or lwIP include, so a host test can run it with fake records.

      Answer  - the record type; `delete` frees it
      read    - bool read(Answer*& out): reads the next record from the packet.
                Success with no record counts as a failure (as in the core).
      handle  - bool handle(const Answer&): false stops the loop

    Returns false at the first failure; records read so far are freed.
*/

#ifndef MDNS_LEAMDNS_STREAM_H
#define MDNS_LEAMDNS_STREAM_H

#include <cstdint>

template <class Answer, class Reader, class Handler>
bool mdnsHandleAnswersOneByOne(uint32_t p_u32Count, Reader p_read, Handler p_handle)
{
    bool bResult = true;
    for (uint32_t u32 = 0; ((bResult) && (u32 < p_u32Count)); ++u32)
    {
        Answer* pAnswer = nullptr;
        if ((bResult = p_read(pAnswer)) && (pAnswer))
        {
            bResult = p_handle(*pAnswer);
        }
        else
        {
            bResult = false;
        }
        delete pAnswer;  // also after a failed read: it may hold a partial record
    }
    return bResult;
}

#endif  // MDNS_LEAMDNS_STREAM_H
