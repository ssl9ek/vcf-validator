/**
 * Copyright 2016 EMBL - European Bioinformatics Institute
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <memory>
#include <iostream>
#include <fstream>

#include <boost/filesystem.hpp>

#include "catch/catch.hpp"
#include "sqlite/sqlite3.h"

#include "vcf/odb_report.hpp"
#include "vcf/file_structure.hpp"
#include "vcf/validator.hpp"
#include "vcf/error.hpp"
#include "vcf/report_writer.hpp"
#include "vcf/sqlite_report.hpp"
#include "vcf/summary_report_writer.hpp"


namespace ebi
{
  bool is_valid(std::string path, std::unique_ptr<ebi::vcf::ReportWriter> output)
  {
      std::ifstream input{path};
      if (!input) {
          throw std::runtime_error("file not found: " + path);
      }
      auto validator = ebi::vcf::build_parser(path, ebi::vcf::ValidationLevel::warning, ebi::vcf::Version::v41, 2);
      std::vector<std::unique_ptr<vcf::ReportWriter>> outputs;
      outputs.push_back(std::move(output));

      return ebi::vcf::is_valid_vcf_file(input, *validator, outputs);
  }
  
  TEST_CASE("Unit test: sqlite", "[output]")
  {
      std::string db_name = "test/input_files/sqlite_test.errors.db";
      sqlite3* db;
      ebi::vcf::SqliteReportRW errorDAO{db_name};

      int rc = sqlite3_open(db_name.c_str(), &db);
      if(rc != SQLITE_OK) {
          sqlite3_close(db);
          throw std::runtime_error(std::string("Can't open database: ") + sqlite3_errmsg(db));
      }
      char *zErrMsg = NULL;
      
      SECTION("Write errors")
      {
          
          ebi::vcf::Error test_error{1, "testing errors"};
          errorDAO.write_error(test_error);
          errorDAO.write_error(test_error);
          errorDAO.close();
          

          int count_errors = -1;
          rc = sqlite3_exec(db, "SELECT count(*) FROM errors", [](void* count, int columns, char**values, char**names) {
              if (values[0] != NULL) {
                  *(int*)count = std::stoi(values[0]);
              }
              return 0;
          }, &count_errors, &zErrMsg);
          if (rc != SQLITE_OK) {
              std::string error_message = std::string("Can't read database: ") + zErrMsg;
              sqlite3_free(zErrMsg);
              throw std::runtime_error(error_message);
          }
          
          CHECK(count_errors == 2);
          
      }
      
      SECTION("Write warnings")
      {
          ebi::vcf::Error test_warning{1, "testing warnings"};
          errorDAO.write_warning(test_warning);
          errorDAO.close();

          int count_warnings = -1;
          rc = sqlite3_exec(db, "SELECT count(*) FROM warnings", [](void* count, int columns, char**values, char**names) {
              if (values[0] != NULL) {
                  *(int*)count = std::stoi(values[0]);
              }
              return 0;
          }, &count_warnings, &zErrMsg);
          if (rc != SQLITE_OK) {
              std::string error_message = std::string("Can't read database: ") + zErrMsg;
              sqlite3_free(zErrMsg);
              throw std::runtime_error(error_message);
          }

          CHECK(count_warnings == 1);
      }
      
      SECTION("Write and count errors")
      {
          ebi::vcf::Error test_error{1, "testing errors"};
          errorDAO.write_error(test_error);
          errorDAO.write_error(test_error);
          errorDAO.flush();
          size_t count_errors = errorDAO.count_errors();
          size_t count_warnings = errorDAO.count_warnings();
          CHECK(count_errors == 2);
          CHECK(count_warnings == 0);
      }

      SECTION("Write and count warnings")
      {
          ebi::vcf::Error test_error{1, "testing warnings"};
          errorDAO.write_warning(test_error);
          errorDAO.flush();
          size_t count_errors = errorDAO.count_errors();
          size_t count_warnings = errorDAO.count_warnings();
          CHECK(count_errors == 0);
          CHECK(count_warnings == 1);
      }
      
      SECTION("Write and read errors")
      {
          size_t line = 8;
          std::string message{"testing errors"};
          ebi::vcf::Error test_error{line, message};
          errorDAO.write_error(test_error);
          errorDAO.flush();
          
          size_t errors_read = 0;
          errorDAO.for_each_error([&](std::shared_ptr<ebi::vcf::Error> error) {
              CHECK(error->line == line);
              CHECK(error->message == message);
              errors_read++;
          });
          CHECK(errors_read == 1);
      }
      
      SECTION("Write and read warnings")
      {
          size_t line = 10;
          std::string message{"testing warnings"};
          ebi::vcf::Error test_error{line, message};
          errorDAO.write_warning(test_error);
          errorDAO.flush();
          
          size_t errors_read = 0;
          errorDAO.for_each_warning([&](std::shared_ptr<ebi::vcf::Error> error) {
              CHECK(error->line == line);
              CHECK(error->message == message);
              errors_read++;
          });
          CHECK(errors_read == 1);
      }
      
      SECTION("Write and read error codes")
      {
          size_t line = 8;
          std::string message{"testing errors"};
          ebi::vcf::Error generic_error{line, message};
          ebi::vcf::MetaSectionError meta_section_error{line, message};
          ebi::vcf::SamplesBodyError samples_body_error{line, message};
          errorDAO.write_error(generic_error);
          errorDAO.write_error(meta_section_error);
          errorDAO.write_error(samples_body_error);
          errorDAO.flush();
          
          std::vector<std::shared_ptr<ebi::vcf::Error>> errors;
          errorDAO.for_each_error([&](std::shared_ptr<ebi::vcf::Error> error) {
              errors.push_back(error);
          });
          
          CHECK(errors.size());
          CHECK(errors[0]->get_code() == ebi::vcf::ErrorCode::error);
          CHECK(errors[1]->get_code() == ebi::vcf::ErrorCode::meta_section);
          CHECK(errors[2]->get_code() == ebi::vcf::ErrorCode::samples_body);
      }
      
      boost::filesystem::path db_file{db_name};
      boost::filesystem::remove(db_file);
      CHECK_FALSE(boost::filesystem::exists(db_file));
  }

  TEST_CASE("Integration test: validator and sqlite", "[output]")
  {
      auto path = boost::filesystem::path("test/input_files/v4.1/failed/failed_fileformat_000.vcf");

      std::string db_name = path.string() + ".errors.db";

      {
          std::unique_ptr<ebi::vcf::ReportWriter> output{new ebi::vcf::SqliteReportRW{db_name}};
          CHECK_FALSE(is_valid(path.string(), std::move(output)));
      }

      SECTION(path.string() + " error count")
      {
          size_t count_errors;
          size_t count_warnings;

          {
              ebi::vcf::SqliteReportRW errorsDAO{db_name};
              count_errors = errorsDAO.count_errors();
              count_warnings = errorsDAO.count_warnings();
          }

          CHECK(count_errors == 1);
          CHECK(count_warnings == 2);
      }

      SECTION(path.string() + " error details")
      {
          size_t errors_read = 0;
          ebi::vcf::SqliteReportRW errorsDAO{db_name};

          errorsDAO.for_each_error([&errors_read](std::shared_ptr<ebi::vcf::Error> error) {
              CHECK(error->line == 1);
              CHECK(error->message == "The fileformat declaration is not 'fileformat=VCFv4.1'");
              errors_read++;
          });

          // do we prefer this?
//          for(ebi::vcf::Error* error : errorsDAO.read_errors()) {
//              CHECK(error->get_line() == 1);
//              CHECK(error->get_raw_message() == "The fileformat declaration is not 'fileformat=VCFv4.1'");
//              errors_read++;
//          }

          CHECK(errors_read == 1);
      }

      boost::filesystem::path db_file{db_name};
      boost::filesystem::remove(db_file);
      CHECK_FALSE(boost::filesystem::exists(db_file));

  }

  TEST_CASE("Unit test: odb", "[output]")
  {

      std::string db_name = "test/input_files/sqlite_test.errors.odb.db";
      ebi::vcf::OdbReportRW errorDAO{db_name};

      SECTION("Write and count errors") {
          ebi::vcf::Error test_error{1, "testing errors"};
          errorDAO.write_error(test_error);
          errorDAO.write_error(test_error);
          errorDAO.flush();
          size_t count_errors = errorDAO.count_errors();
          size_t count_warnings = errorDAO.count_warnings();
          CHECK(count_errors == 2);
          CHECK(count_warnings == 0);
      }

      SECTION("Write and count warnings")
      {
          ebi::vcf::Error test_error{1, "testing warnings"};
          errorDAO.write_warning(test_error);
          errorDAO.flush();
          size_t count_errors = errorDAO.count_errors();
          size_t count_warnings = errorDAO.count_warnings();
          CHECK(count_errors == 0);
          CHECK(count_warnings == 1);
      }

      SECTION("Write and read errors")
      {
          size_t line = 8;
          std::string message{"testing errors"};
          ebi::vcf::Error test_error{line, message};
          errorDAO.write_error(test_error);
          errorDAO.flush();

          size_t errors_read = 0;
          errorDAO.for_each_error([&](std::shared_ptr<ebi::vcf::Error> error) {
              CHECK(error->line == line);
              CHECK(error->message == message);
              errors_read++;
          });
          CHECK(errors_read == 1);
      }

      SECTION("Write and read warnings")
      {
          size_t line = 10;
          std::string message{"testing warnings"};
          ebi::vcf::Error test_error{line, message};
          errorDAO.write_warning(test_error);
          errorDAO.flush();

          size_t errors_read = 0;
          errorDAO.for_each_warning([&](std::shared_ptr<ebi::vcf::Error> error) {
              CHECK(error->line == line);
              CHECK(error->message == message);
              errors_read++;
          });
          CHECK(errors_read == 1);
      }


      SECTION("Write and read error codes")
      {
          size_t line = 8;
          std::string message{"testing errors"};
          ebi::vcf::Error generic_error{line, message};
          ebi::vcf::MetaSectionError meta_section_error{line, message};
          ebi::vcf::SamplesBodyError samples_body_error{line, message};
          errorDAO.write_error(generic_error);
          errorDAO.write_error(meta_section_error);
          errorDAO.write_error(samples_body_error);
          errorDAO.flush();

          std::vector<std::shared_ptr<ebi::vcf::Error>> errors;
          errorDAO.for_each_error([&](std::shared_ptr<ebi::vcf::Error> error) {
              errors.push_back(error);
          });

          CHECK(errors.size());
          CHECK(errors[0]->get_code() == ebi::vcf::ErrorCode::error);
          CHECK(errors[1]->get_code() == ebi::vcf::ErrorCode::meta_section);
          CHECK(errors[2]->get_code() == ebi::vcf::ErrorCode::samples_body);
      }


      boost::filesystem::path db_file{db_name};
      boost::filesystem::remove(db_file);
      CHECK_FALSE(boost::filesystem::exists(db_file));

  }

  TEST_CASE("Integration test: validator and odb", "[output]")
  {
      auto path = boost::filesystem::path("test/input_files/v4.1/failed/failed_fileformat_000.vcf");

      std::string db_name = path.string() + ".errors.db";

      {
          std::unique_ptr<ebi::vcf::ReportWriter> output{new ebi::vcf::OdbReportRW{db_name}};
          CHECK_FALSE(is_valid(path.string(), std::move(output)));
      }

      SECTION(path.string() + " error count")
      {
          size_t count_errors;
          size_t count_warnings;

          {
              ebi::vcf::OdbReportRW errorsDAO{db_name};
              count_errors = errorsDAO.count_errors();
              count_warnings = errorsDAO.count_warnings();
          }

          CHECK(count_errors == 1);
          CHECK(count_warnings == 2);
      }

      SECTION(path.string() + " error details")
      {
          size_t errors_read = 0;
          ebi::vcf::OdbReportRW errorsDAO{db_name};

          errorsDAO.for_each_error([&errors_read](std::shared_ptr<ebi::vcf::Error> error) {
              CHECK(error->line == 1);
              CHECK(error->message == "The fileformat declaration is not 'fileformat=VCFv4.1'");
              errors_read++;
          });

          // do we prefer this?
//          for(ebi::vcf::Error* error : errorsDAO.read_errors()) {
//              CHECK(error->get_line() == 1);
//              CHECK(error->get_raw_message() == "The fileformat declaration is not 'fileformat=VCFv4.1'");
//              errors_read++;
//          }

          CHECK(errors_read == 1);
      }

      boost::filesystem::path db_file{db_name};
      boost::filesystem::remove(db_file);
      CHECK_FALSE(boost::filesystem::exists(db_file));

  }

  TEST_CASE("Unit test: summary report", "[output]")
  {
      SECTION("SummaryTracker should skip repeated NoMetaDefinitionError")
      {
          ebi::vcf::SummaryTracker reporter;
          ebi::vcf::NoMetaDefinitionError error{0, "no definition", "column", "field"};

          REQUIRE(reporter.should_write_report(error)); // first time it should write

          REQUIRE_FALSE(reporter.should_write_report(error)); // second time it should skip

          error.column = "other column";    // now the error is a different one, so it should write
          REQUIRE(reporter.should_write_report(error));
      }

      SECTION("SummaryTracker should write every time important Errors")
      {
          ebi::vcf::SummaryTracker reporter;
          ebi::vcf::BodySectionError error{0, "regular body error"};

          // first time it should write
          REQUIRE(reporter.should_write_report(error));

          // second time it should skip
          REQUIRE(reporter.should_write_report(error));
      }
  }
}
