// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/app_list/search/cros_action_history/cros_action_recorder.h"

#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/files/important_file_writer.h"
#include "base/metrics/metrics_hashes.h"
#include "base/no_destructor.h"
#include "base/strings/string_number_conversions.h"
#include "base/task/post_task.h"
#include "base/threading/scoped_blocking_call.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/ui/app_list/search/cros_action_history/cros_action.pb.h"

namespace app_list {
namespace {

constexpr int kSecondsPerDay = 86400;

void SaveToDiskOnWorkerThread(const CrOSActionHistoryProto actions,
                              const base::FilePath action_filepath) {
  // Loads proto string from local disk.
  std::string proto_str;
  if (!base::ReadFileToString(action_filepath, &proto_str))
    proto_str.clear();

  CrOSActionHistoryProto actions_to_write;
  if (!actions_to_write.ParseFromString(proto_str))
    actions_to_write.Clear();

  actions_to_write.MergeFrom(actions);
  const std::string proto_str_to_write = actions_to_write.SerializeAsString();

  // Create directory if it's not there yet.
  const bool create_directory_sucess =
      base::CreateDirectory(action_filepath.DirName());
  DCHECK(create_directory_sucess)
      << "Error create directory for " << action_filepath;
  const bool write_success = base::ImportantFileWriter::WriteFileAtomically(
      action_filepath, proto_str_to_write, "CrOSActionHistory");
  DCHECK(write_success) << "Error writing action_file " << action_filepath;
}

}  // namespace

constexpr char CrOSActionRecorder::kActionHistoryDir[];
constexpr base::TimeDelta CrOSActionRecorder::kSaveInternal;

CrOSActionRecorder::CrOSActionRecorder()
    : should_log_(false),
      should_hash_(true),
      last_save_timestamp_(base::Time::Now()) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  Profile* profile = ProfileManager::GetPrimaryUserProfile();
  if (profile) {
    profile_path_ = profile->GetPath();
  } else {
    // If profile_path_ is not set, then there is no point to record anything.
    should_log_ = false;
  }

  task_runner_ = base::CreateSequencedTaskRunner(
      {base::ThreadPool(), base::TaskPriority::BEST_EFFORT, base::MayBlock(),
       base::TaskShutdownBehavior::SKIP_ON_SHUTDOWN});
}

CrOSActionRecorder::~CrOSActionRecorder() = default;

CrOSActionRecorder* CrOSActionRecorder::GetCrosActionRecorder() {
  static base::NoDestructor<CrOSActionRecorder> recorder;
  return recorder.get();
}

void CrOSActionRecorder::RecordAction(
    const CrOSAction& action,
    const std::vector<std::pair<std::string, int>>& conditions) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!should_log_)
    return;

  CrOSActionProto& cros_action_proto = *actions_.add_actions();

  // Record action.
  cros_action_proto.set_action_name(
      MaybeHashed(std::get<0>(action), should_hash_));
  cros_action_proto.set_secs_since_epoch(base::Time::Now().ToDoubleT());

  // Record conditions.
  for (const auto& pair : conditions) {
    auto& condition = *cros_action_proto.add_conditions();
    condition.set_name(MaybeHashed(pair.first, should_hash_));
    condition.set_value(pair.second);
  }

  // May flush to disk.
  MaybeFlushToDisk();
}

void CrOSActionRecorder::MaybeFlushToDisk() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  const base::Time now = base::Time::Now();
  if (now - last_save_timestamp_ >= kSaveInternal) {
    last_save_timestamp_ = now;

    // Writes the predictor proto to disk asynchronously.
    const std::string day = base::NumberToString(
        static_cast<int>(now.ToDoubleT() / kSecondsPerDay));
    const base::FilePath action_filepath =
        profile_path_.Append(CrOSActionRecorder::kActionHistoryDir).Append(day);

    task_runner_->PostTask(
        FROM_HERE, base::BindOnce(&SaveToDiskOnWorkerThread,
                                  std::move(actions_), action_filepath));
    actions_.Clear();
  }
}

std::string CrOSActionRecorder::MaybeHashed(const std::string& input,
                                            const bool should_hash) {
  return should_hash ? base::NumberToString(base::HashMetricName(input))
                     : input;
}
}  // namespace app_list
