
// -*- mode: c++; c-basic-offset:4 -*-

// This file is part of the BES

// Copyright (c) 2016 OPeNDAP, Inc.
// Author: James Gallagher <jgallagher@opendap.org>
//
// This library is free software; you can redistribute it and/or
// modify it under the terms of the GNU Lesser General Public
// License as published by the Free Software Foundation; either
// version 2.1 of the License, or (at your option) any later version.
//
// This library is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public
// License along with this library; if not, write to the Free Software
// Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
//
// You can contact OPeNDAP, Inc. at PO Box 112, Saunderstown, RI. 02874-0112.

#include "config.h"

#include <string.h>

#include <string>
#include <cassert>

#include <curl/curl.h>

#include <BESError.h>
#include <BESDebug.h>

#include "DmrppCommon.h"
#include "H4ByteStream.h"
#include "DmrppUtil.h"

using namespace std;

namespace dmrpp {

//##########################################################################
//############################# OLD WAY ####################################
#if 0
/**
 * @brief Callback passed to libcurl to handle reading a single byte.
 *
 * This callback assumes that the size of the data is small enough
 * that all of the bytes will be either read at once or that a local
 * temporary buffer can be used to build up the values.
 *
 * @param buffer Data from libcurl
 * @param size Number of bytes
 * @param nmemb Total size of data in this call is 'size * nmemb'
 * @param data Pointer to this
 * @return The number of bytes read
 */
static size_t dmrpp_write_data(void *buffer, size_t size, size_t nmemb, void *data)
{
    DmrppCommon *dc = reinterpret_cast<DmrppCommon*>(data);

    // rbuf: |******++++++++++----------------------|
    //              ^        ^ bytes_read + nbytes
    //              | bytes_read

    unsigned long long bytes_read = dc->get_bytes_read();
    size_t nbytes = size * nmemb;

    BESDEBUG("dmrpp", "bytes_read: " << bytes_read << ", nbytes: " << nbytes << ", rbuf_size: " << dc->get_rbuf_size() << endl);

    // If this fails, the code will write beyond the buffer.
    assert(bytes_read + nbytes <= dc->get_rbuf_size());

    memcpy(dc->get_rbuf() + bytes_read, buffer, nbytes);

    dc->set_bytes_read(bytes_read + nbytes);

    return nbytes;
}

/**
 * @brief Read data using HTTP/File Range GET
 *
 * @see https://curl.haxx.se/libcurl/c/libcurl.html
 * @param url Get dat from this URL
 * @param range ...and this byte range
 * @param user_data A pinter to a DmrppCommon instance
 */
void curl_read_bytes(const string &url, const string &range, void *user_data)
{
    // See https://curl.haxx.se/libcurl/c/CURLOPT_RANGE.html, etc.

    CURL* curl = curl_easy_init();
    if (curl) {
        CURLcode res = curl_easy_setopt(curl, CURLOPT_URL, url.c_str() /*"http://example.com"*/);
        if (res != CURLE_OK)
            throw BESError(string(curl_easy_strerror(res)), BES_INTERNAL_ERROR, __FILE__, __LINE__);

        // Use CURLOPT_ERRORBUFFER for a human-readable message
        char buf[CURL_ERROR_SIZE];
        res = curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, buf);
        if (res != CURLE_OK)
            throw BESError(string(curl_easy_strerror(res)),BES_INTERNAL_ERROR, __FILE__, __LINE__);

        // get the offset to offset + size bytes
        if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_RANGE, range.c_str() /*"0-199"*/))
            throw BESError(string("HTTP Error: ").append(buf), BES_INTERNAL_ERROR, __FILE__, __LINE__);

        // Pass all data to the 'write_data' function
        if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, dmrpp_write_data))
            throw BESError(string("HTTP Error: ").append(buf), BES_INTERNAL_ERROR, __FILE__, __LINE__);

        // Pass this to write_data as the fourth argument
        if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_WRITEDATA, user_data))
            throw BESError(string("HTTP Error: ").append(buf), BES_INTERNAL_ERROR, __FILE__, __LINE__);

        // Perform the request
        if (CURLE_OK != curl_easy_perform(curl)) {
            curl_easy_cleanup(curl);
            throw BESError(string("HTTP Error: ").append(buf), BES_INTERNAL_ERROR, __FILE__, __LINE__);
        }

        curl_easy_cleanup(curl);
    }
}
#endif

//##########################################################################
//############################# NEW WAY ####################################
/**
 * @brief Callback passed to libcurl to handle reading a single byte.
 *
 * This callback assumes that the size of the data is small enough
 * that all of the bytes will be either read at once or that a local
 * temporary buffer can be used to build up the values.
 *
 * @param buffer Data from libcurl
 * @param size Number of bytes
 * @param nmemb Total size of data in this call is 'size * nmemb'
 * @param data Pointer to this
 * @return The number of bytes read
 */
static size_t h4bytestream_write_data(void *buffer, size_t size, size_t nmemb, void *data)
{
	H4ByteStream *h4bs = reinterpret_cast<H4ByteStream*>(data);

    // rbuf: |******++++++++++----------------------|
    //              ^        ^ bytes_read + nbytes
    //              | bytes_read

    unsigned long long bytes_read = h4bs->get_bytes_read();
    size_t nbytes = size * nmemb;

    BESDEBUG("dmrpp", "h4bytestream_write_data() - bytes_read: " << bytes_read << ", nbytes: " << nbytes << ", rbuf_size: " << h4bs->get_rbuf_size() << endl);

    // If this fails, the code will write beyond the buffer.
    assert(bytes_read + nbytes <= h4bs->get_rbuf_size());

    memcpy(h4bs->get_rbuf() + bytes_read, buffer, nbytes);

    h4bs->set_bytes_read(bytes_read + nbytes);

    return nbytes;
}


/**
 * @brief Read data using HTTP/File Range GET
 *
 * @see https://curl.haxx.se/libcurl/c/libcurl.html
 * @param url Get dat from this URL
 * @param range ...and this byte range
 * @param user_data A pinter to a DmrppCommon instance
 */
void curl_read_byteStream(const string &url, const string &range, void *user_data)
{
    // See https://curl.haxx.se/libcurl/c/CURLOPT_RANGE.html, etc.

    CURL* curl = curl_easy_init();
    if (curl) {
        CURLcode res = curl_easy_setopt(curl, CURLOPT_URL, url.c_str() /*"http://example.com"*/);
        if (res != CURLE_OK)
            throw BESError(string(curl_easy_strerror(res)), BES_INTERNAL_ERROR, __FILE__, __LINE__);

        // Use CURLOPT_ERRORBUFFER for a human-readable message
        char buf[CURL_ERROR_SIZE];
        res = curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, buf);
        if (res != CURLE_OK)
            throw BESError(string(curl_easy_strerror(res)),BES_INTERNAL_ERROR, __FILE__, __LINE__);

        // get the offset to offset + size bytes
        if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_RANGE, range.c_str() /*"0-199"*/))
            throw BESError(string("HTTP Error: ").append(buf), BES_INTERNAL_ERROR, __FILE__, __LINE__);

        // Pass all data to the 'write_data' function
        if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, h4bytestream_write_data))
            throw BESError(string("HTTP Error: ").append(buf), BES_INTERNAL_ERROR, __FILE__, __LINE__);

        // Pass this to write_data as the fourth argument
        if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_WRITEDATA, user_data))
            throw BESError(string("HTTP Error: ").append(buf), BES_INTERNAL_ERROR, __FILE__, __LINE__);

        // Perform the request
        if (CURLE_OK != curl_easy_perform(curl)) {
            curl_easy_cleanup(curl);
            throw BESError(string("HTTP Error: ").append(buf), BES_INTERNAL_ERROR, __FILE__, __LINE__);
        }

        curl_easy_cleanup(curl);
    }
}

}// namespace dmrpp
