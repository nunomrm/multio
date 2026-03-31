/*
 * (C) Copyright 1996- ECMWF.
 *
 * This software is licensed under the terms of the Apache Licence Version 2.0
 * which can be obtained at http://www.apache.org/licenses/LICENSE-2.0.
 * In applying this licence, ECMWF does not waive the privileges and immunities
 * granted to it by virtue of its status as an intergovernmental organisation nor
 * does it submit to any jurisdiction.
 */

#include <unistd.h>
#include <cstring>

#include "eckit/filesystem/TmpFile.h"
#include "eckit/filesystem/PathName.h"
#include "eckit/message/Message.h"
#include "eckit/testing/Test.h"

#include "multio/sink/FileSink.h"

#include "TestDataContent.h"
#include "TestHelpers.h"

#include "eccodes.h"

#include "grib_api_internal.h"
#include "metkit/codes/CodesContent.h"
#include "metkit/codes/UserDataContent.h"
#include <filesystem>

namespace multio::test {

using multio::sink::DataSinkFactory;

grib_handle* FunctionHandleFromFile(const char* grib_path)
{
    char buf[1024] = {0,};
    int e           = 0;
    int i           = 0;
    grib_context* ctx_aux = grib_context_get_default();
    grib_file* file = NULL;
    grib_handle* handle = nullptr;

    file = grib_file_open(grib_path, "r", &e);
    if (!file || !file->handle)
    {
        printf("ERROR: Could not create handle from file \n");
        return nullptr;
    }

    fseeko(file->handle, 0, SEEK_SET);


    handle = grib_handle_new_from_file(ctx_aux, file->handle, &e);

    grib_file_close(file->name, 0, &e);
    return handle;
}

CASE("FileSink exists in factory") {
    // DataSinkFactory::list appends the results to a ostream&, so we need to extract them.
    std::stringstream ss;
    DataSinkFactory::instance().list(ss);
    EXPECT(ss.str().find("file") != std::string::npos);
}

CASE("FileSink is created successfully") {
    const eckit::PathName& file_path = eckit::TmpFile();
    auto sink = make_configured_file_sink(file_path);
    auto fileSink = dynamic_cast<FileSink*>(sink.get());
    EXPECT(fileSink);
    EXPECT(file_path.exists());
}

CASE("FileSink writes correctly") {
    const eckit::PathName& file_path = eckit::TmpFile();
    auto sink = make_configured_file_sink(file_path);
    const char quote[] = "All was quiet in the deep dark wood. The mouse found a nut and the nut was good.";

    eckit::message::Message msg{new TestDataContent{quote, sizeof(quote) - 1}};
    sink->write(msg);

    EXPECT(file_content(file_path) == std::string(quote));
}

CASE("FileSink creates new file by default") {
    const eckit::PathName& file_path = eckit::TmpFile();
    const char quote[] = "All was quiet in the deep dark wood. The mouse found a nut and the nut was good.";

    {
        auto sink = make_configured_file_sink(file_path);
        eckit::message::Message msg{new TestDataContent{quote, sizeof(quote) - 1}};
        sink->write(msg);
    }

    {
        auto sink = make_configured_file_sink(file_path);
        eckit::message::Message msg{new TestDataContent{quote, sizeof(quote) - 1}};
        sink->write(msg);
    }

    EXPECT(file_content(file_path) == std::string(quote));
}


CASE("FileSink creates new file by explicit request") {
    const eckit::PathName& file_path = eckit::TmpFile();
    const char quote[] = "All was quiet in the deep dark wood. The mouse found a nut and the nut was good.";

    {
        auto sink = make_configured_file_sink(file_path);
        eckit::message::Message msg{new TestDataContent{quote, sizeof(quote) - 1}};
        sink->write(msg);
    }

    {
        auto sink = make_configured_file_sink(file_path, false);
        eckit::message::Message msg{new TestDataContent{quote, sizeof(quote) - 1}};
        sink->write(msg);
    }

    EXPECT(file_content(file_path) == std::string(quote));
}

CASE("FileSink appends to existing file") {
    const eckit::PathName& file_path = eckit::TmpFile();
    const char quote[] = "All was quiet in the deep dark wood. The mouse found a nut and the nut was good.";

    {
        auto sink = make_configured_file_sink(file_path);
        eckit::message::Message msg{new TestDataContent{quote, sizeof(quote) - 1}};
        sink->write(msg);
    }

    {
        auto sink = make_configured_file_sink(file_path, true);
        eckit::message::Message msg{new TestDataContent{quote, sizeof(quote) - 1}};
        sink->write(msg);
    }

    EXPECT(file_content(file_path) == std::string{quote} + std::string{quote});
}

CASE("NetcdfSink test") {
    std::string output_netcdf = "/home/maxness/EERIE_Grib_to_Netcdf/tests/grib_files/";
    std::string file_netcdf = "/home/maxness/EERIE_Grib_to_Netcdf/tests/grib_files/_EERIE_IFS-NEMO_historical_r1i1p1f1_ua_2020012000-2020012300-2020012000_level_850.nc";
    if (std::filesystem::remove(file_netcdf))
    {
        std::cout << "Removed netcdf file successfully.\n";
    }
    else
    {
        std::cout << "Failed to delete file_netcdf (it may not exist)";
    }
    const eckit::PathName& file_path = eckit::PathName(output_netcdf);
    const char* grib_file_path[4] = {"/home/maxness/EERIE_Grib_to_Netcdf/tests/grib_files/PL_1Timestep_Level850.grib", "/home/maxness/EERIE_Grib_to_Netcdf/tests/grib_files/PL_1Timestep_Level1000.grib", "/home/maxness/EERIE_Grib_to_Netcdf/tests/grib_files/PL_1Timestep_Level925.grib", "/home/maxness/EERIE_Grib_to_Netcdf/tests/grib_files/PL_1Timestep_Level500.grib"};
    grib_handle* handle = nullptr;
    int timesteps = 1;
    {
        auto sink = make_configured_netcdf_sink(file_path);
        for (int j = 0 ; j < timesteps ; j++)
        {
            handle = FunctionHandleFromFile(grib_file_path[j]);
            eckit::message::Message msg{new metkit::codes::CodesContent{handle, true}};
            std::cout << "Test NetcdfSink action" << std::endl;
            sink->write(msg);
        }
    }
    // grib_handle_delete(handle); // Already cleaned in sink action
    EXPECT(std::filesystem::exists(file_netcdf));
}

}  // namespace multio::test


int main(int argc, char** argv) {
    return eckit::testing::run_tests(argc, argv);
}
