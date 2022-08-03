// Copyright 2015 The Cobalt Authors. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include "base/files/file_util.h"
#include "base/values.h"
#include "cobalt/cache/cache.h"
#include "cobalt/h5vcc/h5vcc_storage.h"
#include "cobalt/persistent_storage/persistent_settings.h"
#include "cobalt/storage/storage_manager.h"
#include "net/disk_cache/cobalt/cobalt_backend_impl.h"
#include "net/disk_cache/cobalt/resource_type.h"
#include "net/http/http_cache.h"
#include "net/http/http_transaction_factory.h"

#include "starboard/common/file.h"
#include "starboard/common/string.h"

namespace cobalt {
namespace h5vcc {

namespace {

const char kTestFileName[] = "cache_test_file.json";

const uint32 kWriteBufferSize = 1024 * 1024;

const uint32 kReadBufferSize = 1024 * 1024;

H5vccStorageWriteTestResponse WriteTestResponse(std::string error = "",
                                                uint32 bytes_written = 0) {
  H5vccStorageWriteTestResponse response;
  response.set_error(error);
  response.set_bytes_written(bytes_written);
  return response;
}

H5vccStorageVerifyTestResponse VerifyTestResponse(std::string error = "",
                                                  bool verified = false,
                                                  uint32 bytes_read = 0) {
  H5vccStorageVerifyTestResponse response;
  response.set_error(error);
  response.set_verified(verified);
  response.set_bytes_read(bytes_read);
  return response;
}

H5vccStorageSetQuotaResponse SetQuotaResponse(std::string error = "",
                                              bool success = false) {
  H5vccStorageSetQuotaResponse response;
  response.set_success(success);
  response.set_error(error);
  return response;
}

}  // namespace

H5vccStorage::H5vccStorage(
    network::NetworkModule* network_module,
    persistent_storage::PersistentSettings* persistent_settings)
    : network_module_(network_module),
      persistent_settings_(persistent_settings) {
  http_cache_ = nullptr;
  if (network_module == nullptr) {
    return;
  }
  auto url_request_context = network_module_->url_request_context();
  if (url_request_context->using_http_cache()) {
    http_cache_ = url_request_context->http_transaction_factory()->GetCache();
  }
}

void H5vccStorage::ClearCookies() {
  net::CookieStore* cookie_store =
      network_module_->url_request_context()->cookie_store();
  auto* cookie_monster = static_cast<net::CookieMonster*>(cookie_store);
  network_module_->task_runner()->PostBlockingTask(
      FROM_HERE,
      base::Bind(&net::CookieMonster::DeleteAllMatchingInfoAsync,
                 base::Unretained(cookie_monster), net::CookieDeletionInfo(),
                 base::Passed(net::CookieStore::DeleteCallback())));
}

void H5vccStorage::Flush(const base::Optional<bool>& sync) {
  if (sync.value_or(false) == true) {
    DLOG(WARNING) << "Synchronous flush is not supported.";
  }

  network_module_->storage_manager()->FlushNow(base::Closure());
}

bool H5vccStorage::GetCookiesEnabled() {
  return network_module_->network_delegate()->cookies_enabled();
}

void H5vccStorage::SetCookiesEnabled(bool enabled) {
  network_module_->network_delegate()->set_cookies_enabled(enabled);
}

H5vccStorageWriteTestResponse H5vccStorage::WriteTest(uint32 test_size,
                                                      std::string test_string) {
  // Get cache_dir path.
  std::vector<char> cache_dir(kSbFileMaxPath + 1, 0);
  SbSystemGetPath(kSbSystemPathCacheDirectory, cache_dir.data(),
                  kSbFileMaxPath);

  // Delete the contents of cache_dir.
  starboard::SbFileDeleteRecursive(cache_dir.data(), true);

  // Try to Create the test_file.
  std::string test_file_path =
      std::string(cache_dir.data()) + kSbFileSepString + kTestFileName;
  SbFileError test_file_error;
  starboard::ScopedFile test_file(test_file_path.c_str(),
                                  kSbFileOpenAlways | kSbFileWrite, NULL,
                                  &test_file_error);

  if (test_file_error != kSbFileOk) {
    return WriteTestResponse(
        starboard::FormatString("SbFileError: %d while opening ScopedFile: %s",
                                test_file_error, test_file_path.c_str()));
  }

  // Repeatedly write test_string to test_size bytes of write_buffer.
  std::string write_buf;
  int iterations = test_size / test_string.length();
  for (int i = 0; i < iterations; ++i) {
    write_buf.append(test_string);
  }
  write_buf.append(test_string.substr(0, test_size % test_string.length()));

  // Incremental Writes of test_data, copies SbWriteAll, using a maximum
  // kWriteBufferSize per write.
  uint32 total_bytes_written = 0;

  do {
    auto bytes_written = test_file.Write(
        write_buf.data() + total_bytes_written,
        std::min(kWriteBufferSize, test_size - total_bytes_written));
    if (bytes_written <= 0) {
      SbFileDelete(test_file_path.c_str());
      return WriteTestResponse("SbWrite -1 return value error");
    }
    total_bytes_written += bytes_written;
  } while (total_bytes_written < test_size);

  test_file.Flush();

  return WriteTestResponse("", total_bytes_written);
}

H5vccStorageVerifyTestResponse H5vccStorage::VerifyTest(
    uint32 test_size, std::string test_string) {
  std::vector<char> cache_dir(kSbFileMaxPath + 1, 0);
  SbSystemGetPath(kSbSystemPathCacheDirectory, cache_dir.data(),
                  kSbFileMaxPath);

  std::string test_file_path =
      std::string(cache_dir.data()) + kSbFileSepString + kTestFileName;
  SbFileError test_file_error;
  starboard::ScopedFile test_file(test_file_path.c_str(),
                                  kSbFileOpenOnly | kSbFileRead, NULL,
                                  &test_file_error);

  if (test_file_error != kSbFileOk) {
    return VerifyTestResponse(
        starboard::FormatString("SbFileError: %d while opening ScopedFile: %s",
                                test_file_error, test_file_path.c_str()));
  }

  // Incremental Reads of test_data, copies SbReadAll, using a maximum
  // kReadBufferSize per write.
  uint32 total_bytes_read = 0;

  do {
    char read_buf[kReadBufferSize];
    auto bytes_read = test_file.Read(
        read_buf, std::min(kReadBufferSize, test_size - total_bytes_read));
    if (bytes_read <= 0) {
      SbFileDelete(test_file_path.c_str());
      return VerifyTestResponse("SbRead -1 return value error");
    }

    // Verify read_buf equivalent to a repeated test_string.
    for (auto i = 0; i < bytes_read; ++i) {
      if (read_buf[i] !=
          test_string[(total_bytes_read + i) % test_string.size()]) {
        return VerifyTestResponse(
            "File test data does not match with test data string");
      }
    }

    total_bytes_read += bytes_read;
  } while (total_bytes_read < test_size);

  if (total_bytes_read != test_size) {
    SbFileDelete(test_file_path.c_str());
    return VerifyTestResponse(
        "File test data size does not match kTestDataSize");
  }

  SbFileDelete(test_file_path.c_str());
  return VerifyTestResponse("", true, total_bytes_read);
}

H5vccStorageSetQuotaResponse H5vccStorage::SetQuota(
    H5vccStorageResourceTypeQuotaBytesDictionary quota) {
  if (!quota.has_other() || !quota.has_html() || !quota.has_css() ||
      !quota.has_image() || !quota.has_font() || !quota.has_splash() ||
      !quota.has_uncompiled_js() || !quota.has_compiled_js()) {
    return SetQuotaResponse(
        "H5vccStorageResourceTypeQuotaBytesDictionary input parameter missing "
        "required fields.");
  }

  if (quota.other() < 0 || quota.html() < 0 || quota.css() < 0 ||
      quota.image() < 0 || quota.font() < 0 || quota.splash() < 0 ||
      quota.uncompiled_js() < 0 || quota.compiled_js() < 0) {
    return SetQuotaResponse(
        "H5vccStorageResourceTypeQuotaBytesDictionary input parameter fields "
        "cannot "
        "have a negative value.");
  }

  auto quota_total = quota.other() + quota.html() + quota.css() +
                     quota.image() + quota.font() + quota.splash() +
                     quota.uncompiled_js() + quota.compiled_js();

  // TODO(b/235529738): Calculate correct max_quota_size that subtracts
  // non-cache memory used in the kSbSystemPathCacheDirectory.
  uint32_t max_quota_size = 24 * 1024 * 1024;
#if SB_API_VERSION >= 14
  max_quota_size = kSbMaxSystemPathCacheDirectorySize;
#endif
  if (quota_total != max_quota_size) {
    return SetQuotaResponse(starboard::FormatString(
        "H5vccStorageResourceTypeQuotaDictionary input parameter field values "
        "sum (%d) is not equal to the max cache size (%d).",
        quota_total, max_quota_size));
  }

  // Write to persistent storage with the new quota values.
  // Static cast value to double since base::Value cannot be a long.
  persistent_settings_->SetPersistentSetting(
      disk_cache::kTypeMetadata[disk_cache::kOther].directory,
      std::make_unique<base::Value>(static_cast<double>(quota.other())));
  persistent_settings_->SetPersistentSetting(
      disk_cache::kTypeMetadata[disk_cache::kHTML].directory,
      std::make_unique<base::Value>(static_cast<double>(quota.html())));
  persistent_settings_->SetPersistentSetting(
      disk_cache::kTypeMetadata[disk_cache::kCSS].directory,
      std::make_unique<base::Value>(static_cast<double>(quota.css())));
  persistent_settings_->SetPersistentSetting(
      disk_cache::kTypeMetadata[disk_cache::kImage].directory,
      std::make_unique<base::Value>(static_cast<double>(quota.image())));
  persistent_settings_->SetPersistentSetting(
      disk_cache::kTypeMetadata[disk_cache::kFont].directory,
      std::make_unique<base::Value>(static_cast<double>(quota.font())));
  persistent_settings_->SetPersistentSetting(
      disk_cache::kTypeMetadata[disk_cache::kSplashScreen].directory,
      std::make_unique<base::Value>(static_cast<double>(quota.splash())));
  persistent_settings_->SetPersistentSetting(
      disk_cache::kTypeMetadata[disk_cache::kUncompiledScript].directory,
      std::make_unique<base::Value>(
          static_cast<double>(quota.uncompiled_js())));
  persistent_settings_->SetPersistentSetting(
      disk_cache::kTypeMetadata[disk_cache::kCompiledScript].directory,
      std::make_unique<base::Value>(static_cast<double>(quota.compiled_js())));

  return SetQuotaResponse("", true);
}

H5vccStorageResourceTypeQuotaBytesDictionary H5vccStorage::GetQuota() {
  // Return persistent storage quota values.
  H5vccStorageResourceTypeQuotaBytesDictionary quota;

  auto other_meta_data = disk_cache::kTypeMetadata[disk_cache::kOther];
  quota.set_other(
      static_cast<uint32>(persistent_settings_->GetPersistentSettingAsDouble(
          other_meta_data.directory,
          other_meta_data.max_size_mb * 1024 * 1024)));

  auto html_meta_data = disk_cache::kTypeMetadata[disk_cache::kHTML];
  quota.set_html(
      static_cast<uint32>(persistent_settings_->GetPersistentSettingAsDouble(
          html_meta_data.directory, html_meta_data.max_size_mb * 1024 * 1024)));

  auto css_meta_data = disk_cache::kTypeMetadata[disk_cache::kCSS];
  quota.set_css(
      static_cast<uint32>(persistent_settings_->GetPersistentSettingAsDouble(
          css_meta_data.directory, css_meta_data.max_size_mb * 1024 * 1024)));

  auto image_meta_data = disk_cache::kTypeMetadata[disk_cache::kImage];
  quota.set_image(
      static_cast<uint32>(persistent_settings_->GetPersistentSettingAsDouble(
          image_meta_data.directory,
          image_meta_data.max_size_mb * 1024 * 1024)));

  auto font_meta_data = disk_cache::kTypeMetadata[disk_cache::kFont];
  quota.set_font(
      static_cast<uint32>(persistent_settings_->GetPersistentSettingAsDouble(
          font_meta_data.directory, font_meta_data.max_size_mb * 1024 * 1024)));

  auto splash_meta_data = disk_cache::kTypeMetadata[disk_cache::kSplashScreen];
  quota.set_splash(
      static_cast<uint32>(persistent_settings_->GetPersistentSettingAsDouble(
          splash_meta_data.directory,
          splash_meta_data.max_size_mb * 1024 * 1024)));

  auto uncompiled_meta_data =
      disk_cache::kTypeMetadata[disk_cache::kUncompiledScript];
  quota.set_uncompiled_js(
      static_cast<uint32>(persistent_settings_->GetPersistentSettingAsDouble(
          uncompiled_meta_data.directory,
          uncompiled_meta_data.max_size_mb * 1024 * 1024)));

  auto compiled_meta_data =
      disk_cache::kTypeMetadata[disk_cache::kCompiledScript];
  quota.set_compiled_js(
      static_cast<uint32>(persistent_settings_->GetPersistentSettingAsDouble(
          compiled_meta_data.directory,
          compiled_meta_data.max_size_mb * 1024 * 1024)));

  // TODO(b/235529738): Calculate correct max_quota_size that subtracts
  // non-cache memory used in the kSbSystemPathCacheDirectory.
  uint32_t max_quota_size = 24 * 1024 * 1024;
#if SB_API_VERSION >= 14
  max_quota_size = kSbMaxSystemPathCacheDirectorySize;
#endif

  quota.set_total(max_quota_size);

  return quota;
}

void H5vccStorage::EnableCache() {
  persistent_settings_->SetPersistentSetting(
      disk_cache::kCacheEnabledPersistentSettingsKey,
      std::make_unique<base::Value>(true));

  cobalt::cache::Cache::GetInstance()->set_enabled(true);

  if (http_cache_) {
    http_cache_->set_mode(net::HttpCache::Mode::NORMAL);
  }
}

void H5vccStorage::DisableCache() {
  persistent_settings_->SetPersistentSetting(
      disk_cache::kCacheEnabledPersistentSettingsKey,
      std::make_unique<base::Value>(false));

  cobalt::cache::Cache::GetInstance()->set_enabled(false);

  if (http_cache_) {
    http_cache_->set_mode(net::HttpCache::Mode::DISABLE);
  }
}

}  // namespace h5vcc
}  // namespace cobalt
