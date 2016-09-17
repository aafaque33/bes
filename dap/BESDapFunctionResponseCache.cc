// -*- mode: c++; c-basic-offset:4 -*-

// This file is part of HYrax, A C++ implementation of the OPeNDAP Data
// Access Protocol.

// Copyright (c) 2016 OPeNDAP, Inc.
// Author: Nathan David Potter <ndp@opendap.org>
//         James Gallagher <jgallagher@opendap.org>
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

//#define DODS_DEBUG

#include <cstdio>
#include <unistd.h>
#include <sys/stat.h>

#include <iostream>
#include <string>
#include <fstream>
#include <sstream>

#ifdef HAVE_TR1_FUNCTIONAL
#include <tr1/functional>
#endif

#include <DDS.h>
#include <ConstraintEvaluator.h>
#include <DDXParserSAX2.h>

#include <XDRStreamMarshaller.h>
#include <XDRStreamUnMarshaller.h>
#include <XDRFileUnMarshaller.h>

#include <D4StreamMarshaller.h>
#include <D4StreamUnMarshaller.h>

#include <Sequence.h>   // We have to special-case these; see read_data_ddx()

#include <debug.h>
#include <mime_util.h>	// for last_modified_time() and rfc_822_date()
#include <util.h>

#include "CacheTypeFactory.h"
#include "CacheMarshaller.h"
#include "CacheUnMarshaller.h"

#include "BESDapFunctionResponseCache.h"
#include "BESDapResponseBuilder.h"
#include "BESInternalError.h"

#include "BESUtil.h"
#include "TheBESKeys.h"
#include "BESLog.h"
#include "BESDebug.h"

#define DEBUG_KEY "response_cache"

#ifdef HAVE_TR1_FUNCTIONAL
#define HASH_OBJ std::tr1::hash
#else
#define HASH_OBJ std::hash
#endif

using namespace std;
using namespace libdap;

const string DATA_MARK = "--DATA:";

// If the size of the constraint is larger then this value, don't cache the response.
const unsigned int max_cacheable_ce_len = 4096;
const unsigned int max_collisions = 50; // It's hard to believe this could happen

const unsigned int default_cache_size = 20; // 20 GB
const string default_cache_prefix = "rc";
const string default_cache_dir = "/tmp/";

const string BESDapFunctionResponseCache::PATH_KEY = "DAP.FunctionResponseCache.path";
const string BESDapFunctionResponseCache::PREFIX_KEY = "DAP.FunctionResponseCache.prefix";
const string BESDapFunctionResponseCache::SIZE_KEY = "DAP.FunctionResponseCache.size";

BESDapFunctionResponseCache *BESDapFunctionResponseCache::d_instance = 0;

unsigned long BESDapFunctionResponseCache::getCacheSizeFromConfig()
{
    bool found;
    string size;
    unsigned long size_in_megabytes = default_cache_size;
    TheBESKeys::TheKeys()->get_value(SIZE_KEY, size, found);
    if (found) {
        BESDEBUG(DEBUG_KEY,
            "BESDapFunctionResponseCache::getCacheSizeFromConfig(): Located BES key " << SIZE_KEY<< "=" << size << endl);
        istringstream iss(size);
        iss >> size_in_megabytes;
    }

    return size_in_megabytes;
}

string BESDapFunctionResponseCache::getCachePrefixFromConfig()
{
    bool found;
    string prefix = default_cache_prefix;
    TheBESKeys::TheKeys()->get_value(PREFIX_KEY, prefix, found);
    if (found) {
        BESDEBUG(DEBUG_KEY,
            "BESDapFunctionResponseCache::getCachePrefixFromConfig(): Located BES key " << PREFIX_KEY<< "=" << prefix << endl);
        prefix = BESUtil::lowercase(prefix);
    }

    return prefix;
}

// If the cache prefix is the empty string, the cache is turned off.
string BESDapFunctionResponseCache::getCacheDirFromConfig()
{
    bool found;

    string cacheDir = default_cache_dir;
    TheBESKeys::TheKeys()->get_value(PATH_KEY, cacheDir, found);
    if (found) {
        BESDEBUG(DEBUG_KEY,
            "BESDapFunctionResponseCache::getCacheDirFromConfig(): Located BES key " << PATH_KEY<< "=" << cacheDir << endl);
    }

    return cacheDir;
}

#if 0
BESDapFunctionResponseCache::BESDapFunctionResponseCache()
{
    BESDEBUG(DEBUG_KEY, "BESDapFunctionResponseCache::BESDapFunctionResponseCache() - BEGIN" << endl);

    string cacheDir = getCacheDirFromConfig();
    string prefix = getCachePrefixFromConfig();
    unsigned long size_in_megabytes = getCacheSizeFromConfig();

    BESDEBUG(DEBUG_KEY,
        "BESDapFunctionResponseCache::BESDapFunctionResponseCache() - Cache config params: " << cacheDir << ", " << prefix << ", " << size_in_megabytes << endl);

    // The required params must be present. If initialize() is not called,
    // then d_cache will stay null and is_available() will return false.
    // Also, the directory 'path' must exist, or d_cache will be null.
    if (!cacheDir.empty() && size_in_megabytes > 0) initialize(cacheDir, prefix, size_in_megabytes);

    BESDEBUG(DEBUG_KEY, "BESDapFunctionResponseCache::BESDapResponseCache() - END" << endl);
}
#endif

/**
 * Get an instance of the BESDapFunctionResponseCache object. This class is a singleton, so the
 * first call to any of three 'get_instance()' methods makes an instance and subsequent calls
 * return a pointer to that instance.
 *
 *
 * @param cache_dir_key Key to use to get the value of the cache directory
 * @param prefix_key Key for the item/file prefix. Each file added to the cache uses this
 * as a prefix so cached items can be easily identified when /tmp is used for the cache.
 * @param size_key How big should the cache be, in megabytes
 * @return A pointer to a BESDapFunctionResponseCache object
 */
BESDapFunctionResponseCache *
BESDapFunctionResponseCache::get_instance(const string &cache_dir, const string &prefix, unsigned long long size)
{
    if (d_instance == 0) {
        if (dir_exists(cache_dir)) {
            try {
                d_instance = new BESDapFunctionResponseCache(cache_dir, prefix, size);
#ifdef HAVE_ATEXIT
                atexit(delete_instance);
#endif
            }
            catch (BESError &be) {
                BESDEBUG(DEBUG_KEY,
                    "BESDapFunctionResponseCache::get_instance(): Failed to obtain cache! msg: " << be.get_message() << endl);
            }
        }
    }

    BESDEBUG(DEBUG_KEY,
        "BESDapFunctionResponseCache::get_instance(dir,prefix,size) - d_instance: " << d_instance << endl);

    return d_instance;
}

/**
 * Get the default instance of the BESDapFunctionResponseCache object. This will read "TheBESKeys" looking for the values
 * of FUNCTION_CACHE_PATH, FUNCTION_CACHE_PREFIX, an FUNCTION_CACHE_SIZE to initialize the cache.
 */
BESDapFunctionResponseCache *
BESDapFunctionResponseCache::get_instance()
{
    if (d_instance == 0) {
        try {
            if (dir_exists(getCacheDirFromConfig())) {

                d_instance = new BESDapFunctionResponseCache(getCacheDirFromConfig(), getCachePrefixFromConfig(),
                    getCacheSizeFromConfig());
#ifdef HAVE_ATEXIT
                atexit(delete_instance);
#endif
            }
        }
        catch (BESError &be) {
            BESDEBUG(DEBUG_KEY,
                "BESDapFunctionResponseCache::get_instance(): Failed to obtain cache! msg: " << be.get_message() << endl);
        }
    }

    BESDEBUG(DEBUG_KEY, "BESDapFunctionResponseCache::get_instance() - d_instance: " << d_instance << endl);

    return d_instance;
}

/**
 * Is the item named by cache_entry_name valid? This code tests that the
 * cache entry is non-zero in size (returns false if that is the case, although
 * that might not be correct) and that the dataset associated with this
 * ResponseBulder instance is at least as old as the cached entry.
 *
 * @param cache_file_name File name of the cached entry
 * @return True if the thing is valid, false otherwise.
 */
bool BESDapFunctionResponseCache::is_valid(const string &cache_file_name, const string &dataset)
{
    // If the cached response is zero bytes in size, it's not valid. This is true
    // because a DAP data object, even if it has no data still has a metadata part.
    // jhrg 10/20/15

    off_t entry_size = 0;
    time_t entry_time = 0;
    struct stat buf;
    if (stat(cache_file_name.c_str(), &buf) == 0) {
        entry_size = buf.st_size;
        entry_time = buf.st_mtime;
    }
    else {
        return false;
    }

    if (entry_size == 0) return false;

    time_t dataset_time = entry_time;
    if (stat(dataset.c_str(), &buf) == 0) {
        dataset_time = buf.st_mtime;
    }

    // Trick: if the d_dataset is not a file, stat() returns error and
    // the times stay equal and the code uses the cache entry.

    // TODO Fix this so that the code can get a LMT from the correct handler.
    if (dataset_time > entry_time) return false;

    return true;
}

string BESDapFunctionResponseCache::get_resource_id(DDS *dds, const string &constraint)
{
    return dds->filename() + "#" + constraint;
}

bool BESDapFunctionResponseCache::can_be_cached(DDS *dds, const string &constraint)
{
    BESDEBUG(DEBUG_KEY, __PRETTY_FUNCTION__ << " constraint + dds->filename() length: "
        << constraint.length() + dds->filename().size() << endl);

    return (constraint.length() + dds->filename().size() <= max_cacheable_ce_len);
}

/**
 * @brief Return a DDS loaded with data that can be serialized back to a client
 *
 * FIXME Repair where this is called in ResponseBuilder
 *
 * @note This method controls the cache lock, ensuring that the cache is
 * unlocked when it returns.
 *
 * This code either
 * @param dds
 * @param constraint
 * @param eval
 * @return
 */
DDS *
BESDapFunctionResponseCache::get_or_cache_dataset(DDS *dds, const string &constraint, ConstraintEvaluator *eval)
{
    // Build the response_id. Since the response content is a function of both the dataset AND the constraint,
    // glue them together to get a unique id for the response.
    string resourceId = dds->filename() + "#" + constraint;

    BESDEBUG(DEBUG_KEY, __PRETTY_FUNCTION__ << " resourceId: '" << resourceId << "'" << endl);

    // Get a hash function for strings
    HASH_OBJ<string> str_hash;

    // Use the hash function to hash the resourceId.
    size_t hashValue = str_hash(resourceId);
    stringstream hashed_id;
    hashed_id << hashValue;

    BESDEBUG(DEBUG_KEY,  __PRETTY_FUNCTION__ << " hashed_id: '" << hashed_id.str() << "'" << endl);

    // Use the parent class's get_cache_file_name() method and its associated machinery to get the file system path for the cache file.
    // We store it in a variable called basename because the value is later extended as part of the collision avoidance code.
    string cache_file_name = BESFileLockingCache::get_cache_file_name(hashed_id.str(), false);

    BESDEBUG(DEBUG_KEY,  __PRETTY_FUNCTION__ << " cache_file_name: '" << cache_file_name << "'" << endl);

    // Does the cached dataset exist? if yes, ret_dds points to it. If no,
    // cache_file_name is updated to be the correct name for write_dataset_
    // to_cache().
    DDS *ret_dds;
    if ((ret_dds = load_from_cache(resourceId, cache_file_name))) {
        BESDEBUG(DEBUG_KEY, __PRETTY_FUNCTION__ << " Data loaded from cache file: " << cache_file_name << endl);
        ret_dds->filename(dds->filename());
    }
    else if ((ret_dds = write_dataset_to_cache(dds, resourceId, constraint, eval, cache_file_name))) {
        BESDEBUG(DEBUG_KEY, __PRETTY_FUNCTION__ << " Data written to cache file: " << cache_file_name << endl);
    }
    // get_read_lock() returns immediately if the file does not exist,
    // but blocks waiting to get a shared lock if the file does exist.
    else if ((ret_dds = load_from_cache(resourceId, cache_file_name))) {
        BESDEBUG(DEBUG_KEY,  __PRETTY_FUNCTION__ << " Data loaded from cache file (2nd try): " << cache_file_name << endl);
        ret_dds->filename(dds->filename());
    }

    BESDEBUG(DEBUG_KEY,__PRETTY_FUNCTION__ << " Used cache_file_name: " << cache_file_name << " for resource ID: " << resourceId << endl);

    return ret_dds;
}

/**
 * @brief Look for a cache hit; load a DDS and its associated data
 *
 * This private method compares the 'resource_id' value with the resource id
 * in the named cache file. If they match, then this cache file contains
 * the data we're after. In that case this code calls read_data_ddx() which
 * allocates a new DDS object and reads its data from the cache file. If
 * the two resource ids don't match, this method returns null.
 *
 * @param resourceId The resource id is a combination of the filename and the
 * function call part of the CE that built the cached response.
 * @param cache_file_name Value-result parameter: The basename of a cache
 * file that _may_ contain the correct response.
 * @return A pointer to a newly allocated DDS that contains data if the cache file
 * held the correct response, null otherwise.
 */
DDS *
BESDapFunctionResponseCache::load_from_cache(const string &resource_id, string &cache_file_name)
{
    BESDEBUG(DEBUG_KEY, __PRETTY_FUNCTION__ << " resourceId: " << resource_id << endl);

    DDS *cached_dds = 0;   // nullptr

    unsigned long suffix_counter = 0;
    bool keep_looking = true;
    do {
        if (suffix_counter > max_collisions) {
            stringstream ss;
            ss << "Cache error! There are " << suffix_counter << " hash collisions for the resource '" << resource_id
                << "' And that is a bad bad thing.";
            throw BESInternalError(ss.str(), __FILE__, __LINE__);
        }

        // Build cache_file_name and cache_id_file_name from baseName
        stringstream cfname;
        cfname << cache_file_name << "_" << suffix_counter++;

        BESDEBUG(DEBUG_KEY, __PRETTY_FUNCTION__ << " candidate cache_file_name: " << cfname.str() << endl);

        int fd; // unused
        if (!get_read_lock(cfname.str(), fd)) {
            // If get_read_lock() returns false, that means the cache file doesn't exist.
            // Set keep_looking to false and exit the loop.
            keep_looking = false;
            // Set the cache file name to the current value of cfname.str() - this is
            // the name that does not exist and should be used by write_dataset_to_cache()
            cache_file_name = cfname.str();
        }
        else {
            // If get_read_lock() returns true, the cache file exists; look and see if
            // it's the correct one. If so, cached_dds will be true and we exit.

            // Read the first line from the cache file and see if it matches the resource id
            ifstream cache_file_istream(cfname.str().c_str());
            char line[max_cacheable_ce_len];
            cache_file_istream.getline(line, max_cacheable_ce_len);
            string cached_resource_id;
            cached_resource_id.assign(line);

            BESDEBUG(DEBUG_KEY, __PRETTY_FUNCTION__ << " cached_resource_id: " << cached_resource_id << endl);

            if (cached_resource_id.compare(resource_id) == 0) {
                // WooHoo Cache Hit!
                BESDEBUG(DEBUG_KEY, "BESDapFunctionResponseCache::load_from_cache() - Cache Hit!" << endl);

                // non-null value value for cached_dds will exit the loop
                cached_dds = read_cached_data(cache_file_istream);
            }

            unlock_and_close(cfname.str());
        }
    } while (!cached_dds && keep_looking);

    BESDEBUG(DEBUG_KEY, __PRETTY_FUNCTION__ << " Cache " << (cached_dds!=0?"HIT":"MISS") << " for: " << cache_file_name << endl);

    return cached_dds;
}

/**
 * Read data from cache. Allocates a new DDS using the given factory.
 *
 */
DDS *
BESDapFunctionResponseCache::read_cached_data(istream &cached_data)
{
    // Build a CachedSequence; all other types are as BaseTypeFactory builds
    CacheTypeFactory factory;
    DDS *fdds = new DDS(&factory);

    BESDEBUG(DEBUG_KEY, __PRETTY_FUNCTION__ << " - BEGIN" << endl);

    // Parse the DDX; throw an exception on error.
    DDXParser ddx_parser(fdds->get_factory());

    // Parse the DDX, reading up to and including the next boundary.
    // Return the CID for the matching data part
    string data_cid; // Not used. jhrg 5/5/16
    try {
        ddx_parser.intern_stream(cached_data, fdds, data_cid, DATA_MARK);
    }
    catch (Error &e) { // Catch the libdap::Error and throw BESInternalError
        throw BESInternalError(e.get_error_message(), __FILE__, __LINE__);
    }

    CacheUnMarshaller um(cached_data);

    for (DDS::Vars_iter i = fdds->var_begin(), e = fdds->var_end(); i != e; ++i) {
        (*i)->deserialize(um, fdds);
    }

    // mark everything as read. And 'to send.' That is, make sure that when a response
    // is retrieved from the cache, all of the variables are marked as 'to be sent.'
    for (DDS::Vars_iter i = fdds->var_begin(), e = fdds->var_end(); i != e; ++i) {
        (*i)->set_read_p(true);
        (*i)->set_send_p(true);

        // For Sequences, deserialize() will update the 'current row number,' which
        // is the correct behavior but which will also confuse serialize(). Reset the
        // current row number here so serialize() can start working from row 0. jhrg 5/13/16
        // Note: Now uses the recursive version of reset_row_number. jhrg 5/16/16
        if ((*i)->type() == dods_sequence_c) {
            static_cast<Sequence*>(*i)->reset_row_number(true);
        }
    }

    BESDEBUG(DEBUG_KEY, __PRETTY_FUNCTION__ << " - END." << endl);

    fdds->set_factory(0);   // Make sure there is no left-over cruft in the returned DDS

    return fdds;
}

/**
 * @brief Evaluate the CE function(s) with the DDS and write and return the result
 *
 * This code assumes that the cache has already been searched for a given
 * cache result and none found. It computes the new result, evaluating the
 * CE function(s) and stores that result in the cache. The result is then
 * returned.
 *
 * @param dds Evaluate the CE function(s) in the context of this DDS.
 * @param resource_id
 * @param func_ce projection function(s) from constraint sent by client.
 * @param eval Is this necessary? Could it be a local object?
 * @param cache_file_name Use this name to store the cached result
 * @return The new DDS.
 */
DDS *
BESDapFunctionResponseCache::write_dataset_to_cache(DDS *dds, const string &resource_id, const string &func_ce,
    ConstraintEvaluator *eval, const string &cache_file_name)
{
    DDS *fdds = 0;  // will hold the return value

    int fd;
    if (create_and_lock(cache_file_name, fd)) {
        // If here, the cache_file_name could not be locked for read access;
        // try to build it. First make an empty files and get an exclusive lock on them.
        BESDEBUG(DEBUG_KEY,__PRETTY_FUNCTION__ << " Caching " << resource_id << ", func_ce: " << func_ce << endl);

        // Get an output stream directed at the locked cache file
        std::ofstream cache_file_ostream(cache_file_name.c_str());
        if (!cache_file_ostream.is_open())
            throw BESInternalError("Could not open '" + cache_file_name + "' to write cached response.", __FILE__, __LINE__);

        // Do The Stuff
        try {
            // Write the resource_id to the first line of the cache file
            cache_file_ostream << resource_id << endl;

            // Evaluate the function
            ConstraintEvaluator func_eval;
            func_eval.parse_constraint(func_ce, *dds);
            fdds = func_eval.eval_function_clauses(*dds);

            dds->print_xml_writer(cache_file_ostream, true, "");

            cache_file_ostream << DATA_MARK << endl;

            // Define the scope of the StreamMarshaller because for some types it will use
            // a child thread to send data and it's dtor will wait for that thread to complete.
            // We want that before we close the output stream (cache_file_stream) jhrg 5/6/16
            {
                ConstraintEvaluator new_ce;
                CacheMarshaller m(cache_file_ostream);

                for (DDS::Vars_iter i = dds->var_begin(); i != dds->var_end(); i++) {
                    if ((*i)->send_p()) {
                        (*i)->serialize(new_ce, *dds, m, false);
                    }
                }
            }

            // Change the exclusive locks on the new file to a shared lock. This keeps
            // other processes from purging the new file and ensures that the reading
            // process can use it.
            exclusive_to_shared_lock(fd);

            // Now update the total cache size info and purge if needed. The new file's
            // name is passed into the purge method because this process cannot detect its
            // own lock on the file.
            unsigned long long size = update_cache_info(cache_file_name);
            if (cache_too_big(size)) update_and_purge(cache_file_name);

            unlock_and_close(cache_file_name);
        }
        catch (...) {
            // Bummer. There was a problem doing The Stuff. Now we gotta clean up.

            // Close the cache file.
            cache_file_ostream.close();

            // And once it's closed, get rid of the cache file
            this->purge_file(cache_file_name);

            // Unlock the cache
            unlock_and_close(cache_file_name);

            // And finally re-throw the error.
            throw;
        }
    }

    return fdds;
}

