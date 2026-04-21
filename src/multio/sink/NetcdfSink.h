/*
 * (C) Copyright 1996- ECMWF.
 *
 * This software is licensed under the terms of the Apache Licence Version 2.0
 * which can be obtained at http://www.apache.org/licenses/LICENSE-2.0.
 * In applying this licence, ECMWF does not waive the privileges and immunities
 * granted to it by virtue of its status as an intergovernmental organisation nor
 * does it submit to any jurisdiction.
 */

/// @author Marvin Axness (BSC)
/// @date May 2024

/*

MultIO class to output in NetCDF


Description: Based on the FileSink class, the NetCDF class converts an eckit::message::Message msg into a grib_handle. 

The grib_handle (or array of grib_handles) is then passed to eccodes function codes_to_netcdf_multio. This eccodes function processes the grib_handle
to then write directly the cmorized NetCDF.

A filename is passed copying CMIP6 format: 

{mip}{dataset_name}{experiment}{ensemble}{grid}{variable_short_name}{start-date}-{end-date}-{current-date}.nc

*/


#pragma once

#include <iosfwd>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include <iomanip>
#include <unordered_map>

#include "eckit/filesystem/PathName.h"
#include "eckit/filesystem/LocalPathName.h"


#include "multio/config/ComponentConfiguration.h"
#include "multio/sink/DataSink.h"

#include "eccodes.h"
#include "metkit/codes/CodesContent.h"
#include "eckit/parser/JSONParser.h"        // Used to parse grib_netcdf_conversion, which associates a grib_id to a netCDF shortname

#include "eckit/types/Date.h"               // Used for filename format


//----------------------------------------------------------------------------------------------------------------------

namespace eckit {
class FileHandle;
}

namespace multio::sink {

class NetcdfSink final : public DataSink {
public:
    explicit NetcdfSink(const config::ComponentConfiguration& compConf);

    ~NetcdfSink() override;

private:  // methods
    void write(eckit::message::Message msg) override;

    void modifyFilename(eckit::message::Message msg);

    void flush() override;

    void print(std::ostream&) const override;

    void buildCache(const eckit::Value& jsonValue); // Function to populate cache

    int Filter(eckit::message::Message msg);

    std::string fclen_to_enddate(std::string startdate);

    std::string CMOR_Filename(eckit::message::Message msg);


    std::string matchGribNetcdf(const std::string& grib_id);       // Returns netcdf_name for a passed grib_id, reads Conversion table

    template <typename T>
    std::string FormatLeadingZero(T number);        // Function to modify dates format

    
private:  // members
    std::string hard_path_;                 // Path received from component configuration
    std::string flexible_path_;             // Path appended due to filename CMOR format requirements
    std::string CMOR_table;
    codes_handle* h_;
    std::unordered_map<std::string, std::string> gribToNetcdfMap;
    size_t n_messages = 1;
    std::string json_conversion;
    eckit::Value jsonValue;
};

//----------------------------------------------------------------------------------------------------------------------

}  // namespace multio::sink
