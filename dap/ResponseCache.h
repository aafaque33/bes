
// -*- mode: c++; c-basic-offset:4 -*-

// This file is part of libdap, A C++ implementation of the OPeNDAP Data
// Access Protocol.

// Copyright (c) 2013 OPeNDAP, Inc.
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
// Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
//
// You can contact OPeNDAP, Inc. at PO Box 112, Saunderstown, RI. 02874-0112.

#ifndef _response_cache_h
#define _response_cache_h

#include <string>

class BESDAPCache;
class ResponseBuilder;

class libdap::DDS;
class libdap::ConstraintEvaluator;
class libdap::BaseTypeFactory;

/**
 * This class is used to cache DAP2 response objects.
 * @author jhrg 5/3/13
 */

class ResponseCache
{
private:
	ResponseCache(const ResponseCache &src);

    bool is_valid(const std::string &cache_file_name, const std::string &dataset);
    void read_data_from_cache(FILE *data, libdap::DDS *fdds);
    libdap::DDS *get_cached_data_ddx(const std::string &cache_file_name, libdap::BaseTypeFactory *factory, const std::string &dataset);

    BESDAPCache *d_cache;

    void initialize(const std::string &cache_path, const std::string &prefix, unsigned long size_in_megabytes);

    friend class ResponseCacheTest;

public:
    /** Initialize the cache using the default values for the cache. */
    ResponseCache();

    /** Initialize the cache.
     *
     * @note Once the underlying cache object is made, calling this has no effect. To change the cache
     * parameters, first delete the cache.
     * @todo Write the delete method.
     *
     * @param cache_path The pathname where responses are stored. If this does not exist, the cache is not
     * initialized
     * @param prefix Use this to prefix each entry in the cache. This is used to differentiate the response
     * cache entries from other entries if other things are cached in the same pathame.
     * @param size_in_megabytes Cache size.
     */
    ResponseCache(const std::string &cache_path, const std::string &prefix,
    		unsigned long size_in_megabytes) : d_cache(0) {
    	initialize(cache_path, prefix, size_in_megabytes);
    }

    virtual ~ResponseCache() {}

    /** Is the ResponseCache configured to cache objects? It is possible
     * to make a ResponseCache object even though the underlying cache
     * software has not been configured (or is intentionally turned off).
     *
     * @return True if the cache can be used, false otherwise.
     */
    bool is_available() { return d_cache != 0; }

    virtual libdap::DDS *read_cached_dataset(libdap::DDS &dds, const std::string &constraint, ResponseBuilder *rb,
    		libdap::ConstraintEvaluator *eval, std::string &cache_token);

    virtual void unlock_and_close(const std::string &cache_token);
};

#endif // _response_cache_h
