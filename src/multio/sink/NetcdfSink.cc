/*
 * (C) Copyright 1996- ECMWF.
 *
 * This software is licensed under the terms of the Apache Licence Version 2.0
 * which can be obtained at http://www.apache.org/licenses/LICENSE-2.0.
 * In applying this licence, ECMWF does not waive the privileges and immunities
 * granted to it by virtue of its status as an intergovernmental organisation nor
 * does it submit to any jurisdiction.
 */

/// @author Marvin Axness
/// @date Sep 2024
#include <fstream>
#include <iosfwd>

#include "multio/sink/DataSink.h"
#include "multio/sink/NetcdfSink.h"
#include "multio/util/logfile_name.h"
#include "eckit/io/DataHandle.h"    // Is this needed?
#include "grib_api_internal.h"   // Needed for grib_handle full definition (if not it will be forward-declared)
#include "multio/util/FailureHandling.h"


using namespace eckit;
//----------------------------------------------------------------------------------------------------------------------

namespace multio::sink {


namespace {
std::string create_path(const config::ComponentConfiguration& compConf) { // Reused from FileSink
    const auto& cfg = compConf.parsedConfig();
    auto path = cfg.getString("path");
    auto expanded_path = compConf.multioConfig().replaceCurly(path);
    eckit::Log::info() << "path = " << expanded_path << std::endl;
    if (cfg.getBool("per-server", false)) {
        eckit::PathName tmp = expanded_path;
        auto dirName = tmp.baseName().asString() == expanded_path ? "" : tmp.dirName().asString() + "/";
        return dirName + util::filename_prefix() + "-" + tmp.baseName().asString();
    }
    return expanded_path;
}

}  // namespace

/*
Constructor will need the following:

- CMOR_table: Specifies which CMOR table needs to be looked, depends on the fields. E.g. Amon, Omon, day, SImon...

*/
NetcdfSink::NetcdfSink(const config::ComponentConfiguration& compConf) :
    DataSink(compConf), hard_path_(create_path(compConf)), h_{nullptr}  {

    const auto& cfg = compConf.parsedConfig();
    eckit::Log::info() << "Getting CMOR table " << std::endl;
    try
    {
        CMOR_table = cfg.getString("CMOR_table");
    }
    catch(const std::exception& e)                                      // TODO: Error handling more efficiently
    {
        eckit::Log::info() << "CMOR_table not set, assuming Amon " << std::endl;
        CMOR_table = "Amon";
    }
    
    n_messages = 0;                                                             // Initializes message counter
    try
    {
        json_conversion = ::getenv("GRIB_NETCDF_CONVERSION"); // Q: Do we want to get this from a env variable or from the plans?

    }
    catch(const std::exception& e)
    {
        eckit::Log::error() << e.what() << '\n';
    }
    
    eckit::Log::info() << "Grib_Netcdf_Conversion path is :: " << json_conversion << std::endl; // TODO: 
    PathName jsonFilePath(json_conversion);
    try 
    {
        jsonValue = JSONParser::decodeFile(jsonFilePath);
        eckit::Log::info() << "Parsed GRIB_NETCDF_CONVERSION JSON file succesfully " << std::endl;
    }
    catch (const std::exception& e) 
    {
        eckit::Log::info() << "Error parsing JSON file: " << e.what() << std::endl; // TODO Error handline, debug output
    }
    buildCache(jsonValue);

}

NetcdfSink::~NetcdfSink() {
    eckit::Log::info() << "Calling NetcdfSink destructor " << std::endl;
}

// Function to build cache from JSON

void NetcdfSink::buildCache(const eckit::Value& jsonValue)
{
    const auto& equivalence = jsonValue["grib_to_netcdf_equivalence"];
    if (!equivalence.isMap())
    {
        eckit::Log::info() << "'grib_to_netcdf_equivalence' not found, cache remains empty" << std::endl;
        return;
    }
    // Iterate over map
    for (size_t i = 0; i < equivalence.keys().size(); i++)
    {
        const std::string& grib_id = equivalence.keys()[i];
        const eckit::Value& entry = equivalence[grib_id];
        if (entry.isMap() && entry.contains("netcdf_name"))
        {
            gribToNetcdfMap[grib_id] = entry["netcdf_name"].as<std::string>();
        }
    }

    eckit::Log::info() << "Cache initialized with " << gribToNetcdfMap.size() << "entries." << std::endl;
}

std::string NetcdfSink::matchGribNetcdf(const std::string& grib_id)
{
    auto it = gribToNetcdfMap.find(grib_id);
    if (it != gribToNetcdfMap.end())
    {
        eckit::Log::info() << "Found cached NetCDF name for " << grib_id << " :: " << it->second << std::endl;
        return it->second;
    }
    eckit::Log::info() << "Key '" << grib_id << "' not found in cache." << std::endl;
    return {};
}
/*

Function to convert a stardate string to a enddate string, reading the forecast length from the environment variable specified in hres:

- HFCLEN: For some reason it doesn't work with lowercase.

- PD: We might be able to get the forecast length another way

*/
std::string NetcdfSink::fclen_to_enddate(std::string startdatetime)
{
    std::string enddate;
    std::string hours;
    std::string hours_digit;
    const char* env_value = ::getenv("HFCLEN");                         // Env var defined in hres

    if(env_value != nullptr)
    {
        hours = ::getenv("HFCLEN");
        eckit::Log::info() << "Value correctly read for hours :: " << hours << std::endl;
    }
    else                                                                        // TODO: Error handling
    {
        hours = "h72";
        eckit::Log::info() << "Value incorrectly read for hours, so using default " << hours << std::endl;
    }

    for (char c : hours)                    // Function to take only the numerical digits: h72->72
    {
        if (std::isdigit(c))
        {
            hours_digit += c;
        }
    }

    std::string startDate = startdatetime.substr(0,8);          // Get first 8 digits: e.g. 20200101 YYYYMMDD
    std::string startTime = startdatetime.substr(8,2);          // Get further 2 digits, for the time HH
    std::string sDateTime = startDate + " " + startTime;        // Format needed for eckit::DateTime

    eckit::DateTime startdate_DateTime(sDateTime);
    eckit::Log::info() << "startdate_DateTime :: " << startdate_DateTime << std::endl;  // TODO: Output to debug, not cout

    long seconds = std::stol(hours_digit);
    seconds = seconds * 3600 + 0.5;                                 // hours to seconds, 0.5 to round

    eckit::DateTime enddate_DateTime = startdate_DateTime + seconds;  // eckit can add seconds to a DateTime object

    eckit::Log::info() << "enddate_DateTime is :: " << enddate_DateTime << std::endl;        // TODO: Output to debug, not cout
    // Reformating forecast enddate

    eckit::Date testDate = enddate_DateTime.date();                 // TODO: Change name, get rid of "test", it might lead to confusion
    eckit::Time testTime = enddate_DateTime.time();

    long testDateMonth = testDate.month();
    std::string testDateMonthString = FormatLeadingZero(testDateMonth);

    long testDateDay = testDate.day();
    std::string testDateDayString = FormatLeadingZero(testDateDay);

    long testTimeHours = testTime.hours();
    std::string testTimeHoursString = FormatLeadingZero(testTimeHours);

    enddate = std::to_string(testDate.year()) + testDateMonthString + testDateDayString + testTimeHoursString;

    return enddate;
}

template <typename T>
std::string NetcdfSink::FormatLeadingZero(T number)  
{
    static_assert(std::is_integral<T>::value, "Template parameter must be integral type");
    std::ostringstream oss;
    oss << std::setw(2) << std::setfill('0') << number;
    return oss.str();
}

/*

Function to return a string for a CMIP6-like filename 

*/
std::string NetcdfSink::CMOR_Filename(eckit::message::Message msg)
{ 
    std::string dateString = std::to_string(msg.getLong("date"));
    long timeLong = msg.getLong("time"); 
    eckit::Log::info() << "startDate :: " << msg.getLong("dataDate") << std::endl;   // TODO: Direct to Debug out                                                                                   
    std::string paramString = std::to_string(msg.getLong("param"));

    std::string nc_name = NetcdfSink::matchGribNetcdf(paramString);    // Returns netcdf name for a given grib id
    eckit::Log::info() << "Getting env variable yyyymmddzz " << std::endl;

    std::string startdate;
    const char* env_value = ::getenv("STARTDATE");                 // Similar issue with HFCLEN, getting env yyyymmddzz didn't work, so defining STARTDATE in hres,

    if(env_value != nullptr)
    {
        startdate = env_value;
        eckit::Log::info() << "Value correctly read for startdate  :: " << startdate << std::endl;
    }
    else                                                          // TODO: Add error handling here
    {
        startdate = "2020012000";                                       
        eckit::Log::info() << "Value incorrectly read for startdate, so using default " << startdate << std::endl;
    }

    std::string enddate = fclen_to_enddate(startdate);

    eckit::Log::info() << "Getting enddate " << enddate << std::endl;        // TODO: debug output

    timeLong = timeLong / 100;                                  // Getting time as HHMM, we don't care about MM

    std::string timeString = FormatLeadingZero(timeLong);
    
    dateString = dateString + timeString;
    return "_EERIE_IFS-NEMO_historical_r1i1p1f1_" + nc_name + "_" + startdate + "-" + enddate + "-" + dateString;
}


/*

TODO: Rename function name, the scope is not clear from the name

TODO: Think of how access of parallel threads might affect this



*/
void NetcdfSink::modifyFilename(eckit::message::Message msg)
{
    std::string levtype = msg.getString("levtype");         // TODO: I think I already got this previously, should I avoid reading it again?
    std::string levelistString;                      // Striing to contain the multiple levels, when present

    if (levtype == "pl")
    {

        levelistString = std::to_string(msg.getLong("levelist"));
        flexible_path_ = hard_path_ + CMOR_Filename(msg) + "_level_" + levelistString + ".nc" ; 
    }
    else if (levtype == "o3d")                  
    {

        levelistString = std::to_string(msg.getLong("level"));
        flexible_path_ = hard_path_ + CMOR_Filename(msg) + "_level_" + levelistString + ".nc";
    } 
    else
    {
        flexible_path_ = hard_path_ + CMOR_Filename(msg) + ".nc";
    }
}


void NetcdfSink::write(eckit::message::Message msg) {
    int e;
    eckit::Log::info() << "Write function is called :: " << std::endl;
    Filter(msg);
    
}

int NetcdfSink::Filter(eckit::message::Message msg) {
    int e;
    size_t data_size = msg.length();
 
    try
    {
        h_ = codes_handle_new_from_message(nullptr, msg.data(), data_size); // This was already done before to convert multio::message to eckit::message
        if (!h_)
        {
            eckit::Log::error() << "Failed to create grib_handle from message " << std::endl;
            throw multio::util::FailureAwareException("Failed to create grib_handle from message in Netcdf pipeline");
        }
        
        n_messages = 1;
        modifyFilename(msg);
        eckit::Log::info() << "Passing nc_name from MultIO to Eccodes " << nc_name << std::endl;
        e = codes_to_netcdf_multio(h_, n_messages, flexible_path_.c_str(), CMOR_table.c_str(), nc_name.c_str());
    }
    catch(const multio::util::FailureAwareException& ex)
    {        
        eckit::Log::error() << "Failed to create grib_handle from message " << std::endl;
        eckit::Log::error() << ex << '\n';
        throw;
    }
    if (h_)                         // Ensures cleanup always occurs
    {
        grib_handle_delete(h_);
    }
    
    return e;
    
}

void NetcdfSink::flush() {
    eckit::Log::info() << "Flushing ";
    print(eckit::Log::info());
    // eckit::Log::enfo() << std::endl;
}

void NetcdfSink::print(std::ostream& os) const {
    os << "NetcdfSink(path=" << hard_path_ << ")";
}

static DataSinkBuilder<NetcdfSink> NetcdfSinkFactorySingleton("netcdf");

}  // namespace multio::sink
