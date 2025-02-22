// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/app_list/search/app_search_provider.h"

#include <stddef.h>

#include <algorithm>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <unordered_set>
#include <utility>

#include "ash/public/cpp/app_list/app_list_config.h"
#include "ash/public/cpp/app_list/app_list_features.h"
#include "ash/public/cpp/app_list/internal_app_id_constants.h"
#include "ash/public/cpp/app_list/tokenized_string.h"
#include "ash/public/cpp/app_list/tokenized_string_match.h"
#include "base/bind.h"
#include "base/callback_list.h"
#include "base/containers/flat_map.h"
#include "base/location.h"
#include "base/macros.h"
#include "base/metrics/field_trial_params.h"
#include "base/metrics/histogram_macros.h"
#include "base/optional.h"
#include "base/single_thread_task_runner.h"
#include "base/strings/utf_string_conversions.h"
#include "base/threading/thread_task_runner_handle.h"
#include "base/time/clock.h"
#include "chrome/browser/apps/app_service/app_service_proxy.h"
#include "chrome/browser/apps/app_service/app_service_proxy_factory.h"
#include "chrome/browser/chromeos/arc/arc_util.h"
#include "chrome/browser/chromeos/crostini/crostini_features.h"
#include "chrome/browser/chromeos/crostini/crostini_manager.h"
#include "chrome/browser/chromeos/crostini/crostini_registry_service.h"
#include "chrome/browser/chromeos/crostini/crostini_registry_service_factory.h"
#include "chrome/browser/chromeos/extensions/gfx_utils.h"
#include "chrome/browser/chromeos/release_notes/release_notes_storage.h"
#include "chrome/browser/extensions/extension_service.h"
#include "chrome/browser/extensions/extension_ui_util.h"
#include "chrome/browser/extensions/extension_util.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/sync/session_sync_service_factory.h"
#include "chrome/browser/ui/app_list/app_list_model_updater.h"
#include "chrome/browser/ui/app_list/arc/arc_app_icon_loader.h"
#include "chrome/browser/ui/app_list/arc/arc_app_list_prefs.h"
#include "chrome/browser/ui/app_list/arc/arc_app_utils.h"
#include "chrome/browser/ui/app_list/chrome_app_list_item.h"
#include "chrome/browser/ui/app_list/crostini/crostini_app_icon_loader.h"
#include "chrome/browser/ui/app_list/extension_app_utils.h"
#include "chrome/browser/ui/app_list/internal_app/internal_app_metadata.h"
#include "chrome/browser/ui/app_list/search/app_service_app_result.h"
#include "chrome/browser/ui/app_list/search/arc_app_result.h"
#include "chrome/browser/ui/app_list/search/crostini_app_result.h"
#include "chrome/browser/ui/app_list/search/extension_app_result.h"
#include "chrome/browser/ui/app_list/search/search_result_ranker/app_search_result_ranker.h"
#include "chrome/browser/ui/app_list/search/search_result_ranker/ranking_item_util.h"
#include "chrome/browser/ui/app_list/search/search_utils/fuzzy_tokenized_string_match.h"
#include "chrome/common/chrome_features.h"
#include "chrome/common/pref_names.h"
#include "chrome/grit/generated_resources.h"
#include "components/sync/base/model_type.h"
#include "components/sync_sessions/session_sync_service.h"
#include "extensions/browser/extension_prefs.h"
#include "extensions/browser/extension_registry.h"
#include "extensions/browser/extension_system.h"
#include "extensions/common/extension.h"
#include "extensions/common/extension_set.h"
#include "ui/base/l10n/l10n_util.h"

using extensions::ExtensionRegistry;

namespace {

// The minimum capacity we reserve in the Apps container which will be filled
// with extensions and ARC apps, to avoid successive reallocation.
constexpr size_t kMinimumReservedAppsContainerCapacity = 60U;

// Relevance threshold to use when Crostini has not yet been enabled. This value
// is somewhat arbitrary, but is roughly equivalent to the 'ter' in 'terminal'.
constexpr double kCrostiniTerminalRelevanceThreshold = 0.8;

// Adds |app_result| to |results| only in case no duplicate apps were already
// added. Duplicate means the same app but for different domain, Chrome and
// Android.
void MaybeAddResult(app_list::SearchProvider::Results* results,
                    std::unique_ptr<app_list::AppResult> app_result,
                    std::set<std::string>* seen_or_filtered_apps) {
  if (seen_or_filtered_apps->count(app_result->app_id()))
    return;

  seen_or_filtered_apps->insert(app_result->app_id());

  std::unordered_set<std::string> duplicate_app_ids;
  if (!extensions::util::GetEquivalentInstalledArcApps(
          app_result->profile(), app_result->app_id(), &duplicate_app_ids)) {
    results->emplace_back(std::move(app_result));
    return;
  }

  for (const auto& duplicate_app_id : duplicate_app_ids) {
    if (seen_or_filtered_apps->count(duplicate_app_id))
      return;
  }

  results->emplace_back(std::move(app_result));

  // Add duplicate ids in order to filter them if they appear down the
  // list.
  seen_or_filtered_apps->insert(duplicate_app_ids.begin(),
                                duplicate_app_ids.end());
}

// Linearly maps |score| to the range [min, max].
// |score| is assumed to be within [0.0, 1.0]; if it's greater than 1.0
// then max is returned; if it's less than 0.0, then min is returned.
float ReRange(const float score, const float min, const float max) {
  if (score >= 1.0f)
    return max;
  if (score <= 0.0f)
    return min;

  return min + score * (max - min);
}

}  // namespace

namespace app_list {

class AppSearchProvider::App {
 public:
  App(AppSearchProvider::DataSource* data_source,
      const std::string& id,
      const std::string& name,
      const base::Time& last_launch_time,
      const base::Time& install_time,
      bool installed_internally)
      : data_source_(data_source),
        id_(id),
        name_(base::UTF8ToUTF16(name)),
        last_launch_time_(last_launch_time),
        install_time_(install_time),
        installed_internally_(installed_internally) {}
  ~App() = default;

  struct CompareByLastActivityTimeAndThenAppId {
    bool operator()(const std::unique_ptr<App>& app1,
                    const std::unique_ptr<App>& app2) {
      // Sort decreasing by last activity time, then increasing by App ID.
      base::Time t1 = app1->GetLastActivityTime();
      base::Time t2 = app2->GetLastActivityTime();
      if (t1 != t2)
        return t1 > t2;
      return app1->id_ < app2->id_;
    }
  };

  ash::TokenizedString* GetTokenizedIndexedName() {
    // Tokenizing a string is expensive. Don't pay the price for it at
    // construction of every App, but rather, only when needed (i.e. when the
    // query is not empty and cache the result.
    if (!tokenized_indexed_name_)
      tokenized_indexed_name_ = std::make_unique<ash::TokenizedString>(name_);
    return tokenized_indexed_name_.get();
  }

  base::Time GetLastActivityTime() const {
    if (!last_launch_time_.is_null())
      return last_launch_time_;
    if (!installed_internally_)
      return install_time_;
    return base::Time();
  }

  bool MatchSearchableText(const ash::TokenizedString& query) {
    if (searchable_text_.empty())
      return false;
    if (tokenized_indexed_searchable_text_.empty()) {
      for (const base::string16& curr_text : searchable_text_) {
        tokenized_indexed_searchable_text_.push_back(
            std::make_unique<ash::TokenizedString>(curr_text));
      }
    }
    ash::TokenizedStringMatch match;
    for (auto& curr_text : tokenized_indexed_searchable_text_) {
      match.Calculate(query, *curr_text);
      if (match.relevance() > relevance_threshold())
        return true;
    }
    return false;
  }

  AppSearchProvider::DataSource* data_source() { return data_source_; }
  const std::string& id() const { return id_; }
  const base::string16& name() const { return name_; }
  const base::Time& last_launch_time() const { return last_launch_time_; }
  const base::Time& install_time() const { return install_time_; }

  bool recommendable() const { return recommendable_; }
  void set_recommendable(bool recommendable) { recommendable_ = recommendable; }

  bool searchable() const { return searchable_; }
  void set_searchable(bool searchable) { searchable_ = searchable; }

  const std::vector<base::string16>& searchable_text() const {
    return searchable_text_;
  }
  void AddSearchableText(const base::string16& searchable_text) {
    DCHECK(tokenized_indexed_searchable_text_.empty());
    searchable_text_.push_back(searchable_text);
  }

  // Relevance must exceed the threshold to appear as a search result. Exact
  // matches are always surfaced.
  float relevance_threshold() const { return relevance_threshold_; }
  void set_relevance_threshold(float threshold) {
    relevance_threshold_ = threshold;
  }

  bool installed_internally() const { return installed_internally_; }

 private:
  AppSearchProvider::DataSource* data_source_;
  std::unique_ptr<ash::TokenizedString> tokenized_indexed_name_;
  std::vector<std::unique_ptr<ash::TokenizedString>>
      tokenized_indexed_searchable_text_;
  const std::string id_;
  const base::string16 name_;
  const base::Time last_launch_time_;
  const base::Time install_time_;
  bool recommendable_ = true;
  bool searchable_ = true;
  std::vector<base::string16> searchable_text_;
  float relevance_threshold_ = 0.f;
  // Set to true in case app was installed internally, by sync, policy or as a
  // default app.
  const bool installed_internally_;

  DISALLOW_COPY_AND_ASSIGN(App);
};

class AppSearchProvider::DataSource {
 public:
  DataSource(Profile* profile, AppSearchProvider* owner)
      : profile_(profile), owner_(owner) {}
  virtual ~DataSource() {}

  virtual void AddApps(Apps* apps) = 0;

  virtual std::unique_ptr<AppResult> CreateResult(
      const std::string& app_id,
      AppListControllerDelegate* list_controller,
      bool is_recommended) = 0;

  virtual void ViewClosing() {}

 protected:
  Profile* profile() { return profile_; }
  AppSearchProvider* owner() { return owner_; }

 private:
  // Unowned pointers.
  Profile* profile_;
  AppSearchProvider* owner_;

  DISALLOW_COPY_AND_ASSIGN(DataSource);
};

namespace {

class AppServiceDataSource : public AppSearchProvider::DataSource,
                             public apps::AppRegistryCache::Observer {
 public:
  AppServiceDataSource(Profile* profile, AppSearchProvider* owner)
      : AppSearchProvider::DataSource(profile, owner),
        icon_cache_(apps::AppServiceProxyFactory::GetForProfile(profile),
                    apps::IconCache::GarbageCollectionPolicy::kExplicit) {
    apps::AppServiceProxy* proxy =
        apps::AppServiceProxyFactory::GetForProfile(profile);
    if (proxy) {
      Observe(&proxy->AppRegistryCache());
    }

    sync_sessions::SessionSyncService* service =
        SessionSyncServiceFactory::GetInstance()->GetForProfile(profile);
    if (!service)
      return;
    // base::Unretained() is safe below because the subscription itself is a
    // class member field and handles destruction well.
    foreign_session_updated_subscription_ =
        service->SubscribeToForeignSessionsChanged(base::BindRepeating(
            &AppSearchProvider::RefreshAppsAndUpdateResultsDeferred,
            base::Unretained(owner)));
  }

  ~AppServiceDataSource() override = default;

  // AppSearchProvider::DataSource overrides:
  void AddApps(AppSearchProvider::Apps* apps_vector) override {
    apps::AppServiceProxy* proxy =
        apps::AppServiceProxyFactory::GetForProfile(profile());
    if (!proxy) {
      return;
    }
    proxy->AppRegistryCache().ForEachApp([this, apps_vector](
                                             const apps::AppUpdate& update) {
      if (update.Readiness() == apps::mojom::Readiness::kUninstalledByUser)
        return;

      if (!std::strcmp(update.AppId().c_str(),
                       ash::kInternalAppIdContinueReading)) {
        // Continue reading depends on the tab of session from other devices.
        // This checking can be moved to built_in_app, however, it's more
        // reasonable to leave it in search result code, because the status of
        // continue reading is not changed. It depends on the session sync
        // result to decide whether it should be shown in the recommended
        // result, so leave the code in the search result part.
        sync_sessions::SessionSyncService* service =
            SessionSyncServiceFactory::GetInstance()->GetForProfile(profile());
        if (!service || (!service->GetOpenTabsUIDelegate() &&
                         !owner()->open_tabs_ui_delegate_for_testing())) {
          return;
        }
      }

      // TODO(crbug.com/826982): add the "can load in incognito" concept to
      // the App Service and use it here, similar to ExtensionDataSource.

      apps_vector->emplace_back(std::make_unique<AppSearchProvider::App>(
          this, update.AppId(), update.ShortName(), update.LastLaunchTime(),
          update.InstallTime(),
          update.InstalledInternally() == apps::mojom::OptionalBool::kTrue));
      apps_vector->back()->set_recommendable(update.Recommendable() ==
                                             apps::mojom::OptionalBool::kTrue);
      apps_vector->back()->set_searchable(update.Searchable() ==
                                          apps::mojom::OptionalBool::kTrue);

      // Until it's been installed, the Crostini Terminal is hidden and
      // requires a few characters before being shown in search results.
      if ((update.AppType() == apps::mojom::AppType::kCrostini) &&
          (update.AppId() == crostini::kCrostiniTerminalId) &&
          !crostini::CrostiniFeatures::Get()->IsEnabled(profile())) {
        apps_vector->back()->set_recommendable(false);
        apps_vector->back()->set_relevance_threshold(
            kCrostiniTerminalRelevanceThreshold);
      }

      for (const std::string& term : update.AdditionalSearchTerms()) {
        apps_vector->back()->AddSearchableText(base::UTF8ToUTF16(term));
      }
    });
  }

  std::unique_ptr<AppResult> CreateResult(
      const std::string& app_id,
      AppListControllerDelegate* list_controller,
      bool is_recommended) override {
    return std::make_unique<AppServiceAppResult>(
        profile(), app_id, list_controller, is_recommended, &icon_cache_);
  }

  void ViewClosing() override { icon_cache_.SweepReleasedIcons(); }

 private:
  // apps::AppRegistryCache::Observer overrides:
  void OnAppUpdate(const apps::AppUpdate& update) override {
    if (update.Readiness() == apps::mojom::Readiness::kReady) {
      owner()->RefreshAppsAndUpdateResultsDeferred();
    } else {
      owner()->RefreshAppsAndUpdateResults();
    }
  }

  void OnAppRegistryCacheWillBeDestroyed(
      apps::AppRegistryCache* cache) override {
    Observe(nullptr);
  }

  // The AppServiceDataSource seems like one (but not the only) good place to
  // add an App Service icon caching wrapper, because (1) the AppSearchProvider
  // destroys and creates multiple search results in a short period of time,
  // while the user is typing, so will clearly benefit from a cache, and (2)
  // there is an obvious point in time when the cache can be emptied: the user
  // will obviously stop typing (so stop triggering LoadIcon requests) when the
  // search box view closes.
  //
  // There are reasons to have more than one icon caching layer. See the
  // comments for the apps::IconCache::GarbageCollectionPolicy enum.
  apps::IconCache icon_cache_;

  std::unique_ptr<base::CallbackList<void()>::Subscription>
      foreign_session_updated_subscription_;

  DISALLOW_COPY_AND_ASSIGN(AppServiceDataSource);
};

class ExtensionDataSource : public AppSearchProvider::DataSource,
                            public extensions::ExtensionRegistryObserver {
 public:
  ExtensionDataSource(Profile* profile, AppSearchProvider* owner)
      : AppSearchProvider::DataSource(profile, owner),
        extension_registry_observer_(this) {
    extension_registry_observer_.Add(ExtensionRegistry::Get(profile));
  }
  ~ExtensionDataSource() override {}

  // AppSearchProvider::DataSource overrides:
  void AddApps(AppSearchProvider::Apps* apps) override {
    ExtensionRegistry* registry = ExtensionRegistry::Get(profile());
    AddApps(apps, registry->enabled_extensions());
    AddApps(apps, registry->disabled_extensions());
    AddApps(apps, registry->terminated_extensions());
  }

  std::unique_ptr<AppResult> CreateResult(
      const std::string& app_id,
      AppListControllerDelegate* list_controller,
      bool is_recommended) override {
    return std::make_unique<ExtensionAppResult>(
        profile(), app_id, list_controller, is_recommended);
  }

  // extensions::ExtensionRegistryObserver overrides:
  void OnExtensionLoaded(content::BrowserContext* browser_context,
                         const extensions::Extension* extension) override {
    owner()->RefreshAppsAndUpdateResultsDeferred();
  }

  void OnExtensionUninstalled(content::BrowserContext* browser_context,
                              const extensions::Extension* extension,
                              extensions::UninstallReason reason) override {
    owner()->RefreshAppsAndUpdateResults();
  }

 private:
  void AddApps(AppSearchProvider::Apps* apps,
               const extensions::ExtensionSet& extensions) {
    extensions::ExtensionPrefs* prefs =
        extensions::ExtensionPrefs::Get(profile());
    for (const auto& it : extensions) {
      const extensions::Extension* extension = it.get();

      if (!app_list::ShouldShowInLauncher(extension, profile())) {
        continue;
      }

      if (profile()->IsOffTheRecord() &&
          !extensions::util::CanLoadInIncognito(extension, profile())) {
        continue;
      }

      // On first login for a given user on a new device, set |last_launch_time|
      // for OEM app results to Time::Now() so they have a higher relevance than
      // synced apps.
      if (profile()->IsNewProfile() &&
          prefs->WasInstalledByOem(extension->id())) {
        prefs->SetLastLaunchTime(extension->id(), base::Time::Now());
      }

      apps->emplace_back(std::make_unique<AppSearchProvider::App>(
          this, extension->id(), extension->short_name(),
          prefs->GetLastLaunchTime(extension->id()),
          prefs->GetInstallTime(extension->id()),
          extension->was_installed_by_default() ||
              extension->was_installed_by_oem() ||
              extensions::Manifest::IsComponentLocation(
                  extension->location()) ||
              extensions::Manifest::IsPolicyLocation(extension->location())));
    }
  }

  ScopedObserver<extensions::ExtensionRegistry,
                 extensions::ExtensionRegistryObserver>
      extension_registry_observer_;

  DISALLOW_COPY_AND_ASSIGN(ExtensionDataSource);
};

class AppIconDataSource : public AppIconLoaderDelegate {
 public:
  class Delegate {
   public:
    virtual std::unique_ptr<AppIconLoader> CreateIconLoader(
        int icon_size,
        AppIconLoaderDelegate* delegate) = 0;
    virtual void IconUpdated(const std::string& app_id,
                             const gfx::ImageSkia& image,
                             bool for_chip) = 0;
  };

  AppIconDataSource(Delegate* delegate, bool for_chip)
      : delegate_(delegate),
        icon_loader_(delegate->CreateIconLoader(IconSize(for_chip), this)),
        for_chip_(for_chip) {}
  ~AppIconDataSource() override = default;

  void MaybeFetchIcon(const std::string& app_id) {
    auto iter = icon_map_.find(app_id);
    if (iter != icon_map_.end()) {
      delegate_->IconUpdated(app_id, iter->second, for_chip_);
    } else {
      icon_loader_->FetchImage(app_id);
    }
  }

  void RemoveIcon(const std::string& app_id) {
    icon_map_.erase(app_id);
    icon_loader_->ClearImage(app_id);
  }

 private:
  int IconSize(bool for_chip) const {
    const auto& config = ash::AppListConfig::instance();
    return for_chip ? config.suggestion_chip_icon_dimension()
                    : config.GetPreferredIconDimension(
                          ash::SearchResultDisplayType::kTile);
  }

  void OnAppImageUpdated(const std::string& app_id,
                         const gfx::ImageSkia& image) override {
    icon_map_[app_id] = image;
    delegate_->IconUpdated(app_id, image, for_chip_);
  }

  Delegate* delegate_;
  std::unique_ptr<AppIconLoader> icon_loader_;
  std::map<std::string, gfx::ImageSkia> icon_map_;
  bool for_chip_;

  DISALLOW_COPY_AND_ASSIGN(AppIconDataSource);
};

class IconCachedDataSource : public AppSearchProvider::DataSource,
                             public AppIconDataSource::Delegate {
 public:
  IconCachedDataSource(Profile* profile, AppSearchProvider* owner)
      : AppSearchProvider::DataSource(profile, owner) {}
  ~IconCachedDataSource() override = default;

 protected:
  void InitIconSources() {
    icon_source_ = std::make_unique<AppIconDataSource>(this, false);
    chip_icon_source_ = std::make_unique<AppIconDataSource>(this, true);
  }

  std::unique_ptr<AppResult> WrapResult(std::unique_ptr<AppResult> result) {
    const std::string& app_id = result->app_id();
    results_[app_id].push_back(result->GetWeakPtr());
    icon_source_->MaybeFetchIcon(app_id);
    if (result->display_type() == ash::SearchResultDisplayType::kRecommendation)
      chip_icon_source_->MaybeFetchIcon(app_id);
    return result;
  }

  void RemoveIcon(const std::string& app_id) {
    icon_source_->RemoveIcon(app_id);
    chip_icon_source_->RemoveIcon(app_id);
  }

  void RevokeInvalidated() {
    for (auto& p : results_) {
      for (auto iter = p.second.begin(); iter != p.second.end();) {
        if (iter->get()) {
          ++iter;
        } else {
          iter = p.second.erase(iter);
        }
      }
    }
  }

 private:
  // AppIconDataSource::Delegate:
  void IconUpdated(const std::string& app_id,
                   const gfx::ImageSkia& image,
                   bool for_chip) override {
    for (auto result : results_[app_id]) {
      auto* ptr = result.get();
      if (!ptr)
        continue;
      // There's a chance that both kRecommendation results and kTile results
      // are in |results_| but we don't need chip icons for kTile results.
      if (for_chip && ptr->display_type() !=
                          ash::SearchResultDisplayType::kRecommendation) {
        continue;
      }
      if (for_chip)
        ptr->SetChipIcon(image);
      else
        ptr->SetIcon(image);
    }
  }

  std::unique_ptr<AppIconDataSource> icon_source_;
  std::unique_ptr<AppIconDataSource> chip_icon_source_;
  base::flat_map<std::string, std::vector<base::WeakPtr<AppResult>>> results_;

  DISALLOW_COPY_AND_ASSIGN(IconCachedDataSource);
};

class ArcDataSource : public IconCachedDataSource,
                      public ArcAppListPrefs::Observer {
 public:
  ArcDataSource(Profile* profile, AppSearchProvider* owner)
      : IconCachedDataSource(profile, owner) {
    ArcAppListPrefs::Get(profile)->AddObserver(this);
    InitIconSources();
  }

  ~ArcDataSource() override {
    ArcAppListPrefs::Get(profile())->RemoveObserver(this);
  }

  // AppSearchProvider::DataSource overrides:
  void AddApps(AppSearchProvider::Apps* apps) override {
    RevokeInvalidated();
    ArcAppListPrefs* arc_prefs = ArcAppListPrefs::Get(profile());
    CHECK(arc_prefs);

    const std::vector<std::string> app_ids = arc_prefs->GetAppIds();
    for (const auto& app_id : app_ids) {
      std::unique_ptr<ArcAppListPrefs::AppInfo> app_info =
          arc_prefs->GetApp(app_id);
      if (!app_info) {
        NOTREACHED();
        continue;
      }

      // On first login for a given user on a new device, set |last_launch_time|
      // for OEM app results to Time::Now() so they have a higher relevance than
      // synced apps.
      if (profile()->IsNewProfile() && arc_prefs->IsOem(app_id))
        arc_prefs->SetLastLaunchTime(app_id);

      if (!app_info->show_in_launcher)
        continue;

      apps->emplace_back(std::make_unique<AppSearchProvider::App>(
          this, app_id, app_info->name, app_info->last_launch_time,
          app_info->install_time,
          arc_prefs->IsDefault(app_id) ||
              arc_prefs->IsControlledByPolicy(app_info->package_name)));
    }
  }

  std::unique_ptr<AppResult> CreateResult(
      const std::string& app_id,
      AppListControllerDelegate* list_controller,
      bool is_recommended) override {
    return WrapResult(std::make_unique<ArcAppResult>(
        profile(), app_id, list_controller, is_recommended));
  }

  // ArcAppListPrefs::Observer overrides:
  void OnAppRegistered(const std::string& app_id,
                       const ArcAppListPrefs::AppInfo& app_info) override {
    owner()->RefreshAppsAndUpdateResultsDeferred();
  }

  void OnAppStatesChanged(const std::string& app_id,
                          const ArcAppListPrefs::AppInfo& app_info) override {
    RemoveIcon(app_id);
    owner()->RefreshAppsAndUpdateResultsDeferred();
  }

  void OnAppRemoved(const std::string& app_id) override {
    RemoveIcon(app_id);
    owner()->RefreshAppsAndUpdateResults();
  }

  void OnAppNameUpdated(const std::string& app_id,
                        const std::string& name) override {
    owner()->RefreshAppsAndUpdateResultsDeferred();
  }

 private:
  // AppIconDataSource::Delegate:
  std::unique_ptr<AppIconLoader> CreateIconLoader(
      int icon_size,
      AppIconLoaderDelegate* delegate) override {
    return std::make_unique<ArcAppIconLoader>(profile(), icon_size, delegate);
  }

  DISALLOW_COPY_AND_ASSIGN(ArcDataSource);
};

class CrostiniDataSource : public IconCachedDataSource,
                           public crostini::CrostiniRegistryService::Observer {
 public:
  CrostiniDataSource(Profile* profile, AppSearchProvider* owner)
      : IconCachedDataSource(profile, owner) {
    crostini::CrostiniRegistryServiceFactory::GetForProfile(profile)
        ->AddObserver(this);
    InitIconSources();
  }

  ~CrostiniDataSource() override {
    crostini::CrostiniRegistryServiceFactory::GetForProfile(profile())
        ->RemoveObserver(this);
  }

  // AppSearchProvider::DataSource overrides:
  void AddApps(AppSearchProvider::Apps* apps) override {
    RevokeInvalidated();
    crostini::CrostiniRegistryService* registry_service =
        crostini::CrostiniRegistryServiceFactory::GetForProfile(profile());
    for (const auto& pair : registry_service->GetRegisteredApps()) {
      const std::string& app_id = pair.first;
      const auto& registration = pair.second;
      if (registration.NoDisplay())
        continue;
      apps->emplace_back(std::make_unique<AppSearchProvider::App>(
          this, app_id, registration.Name(), registration.LastLaunchTime(),
          registration.InstallTime(), false /* installed_internally */));
      const std::string& executable_file_name =
          registration.ExecutableFileName();
      if (!executable_file_name.empty())
        apps->back()->AddSearchableText(
            base::UTF8ToUTF16(executable_file_name));
      for (const std::string& keyword : registration.Keywords())
        apps->back()->AddSearchableText(base::UTF8ToUTF16(keyword));

      if (app_id == crostini::kCrostiniTerminalId) {
        // Until it's been installed, the Terminal is hidden and requires
        // a few characters before being shown in search results.
        if (!crostini::CrostiniFeatures::Get()->IsEnabled(profile())) {
          apps->back()->set_recommendable(false);
          apps->back()->set_relevance_threshold(
              kCrostiniTerminalRelevanceThreshold);
        }
      }
    }
  }

  std::unique_ptr<AppResult> CreateResult(
      const std::string& app_id,
      AppListControllerDelegate* list_controller,
      bool is_recommended) override {
    return WrapResult(std::make_unique<CrostiniAppResult>(
        profile(), app_id, list_controller, is_recommended));
  }

  // crostini::CrostiniRegistryService::Observer overrides:
  void OnRegistryUpdated(
      crostini::CrostiniRegistryService* registry_service,
      const std::vector<std::string>& updated_apps,
      const std::vector<std::string>& removed_apps,
      const std::vector<std::string>& inserted_apps) override {
    for (const auto& id : updated_apps)
      RemoveIcon(id);
    for (const auto& id : removed_apps)
      RemoveIcon(id);
    if (removed_apps.empty())
      owner()->RefreshAppsAndUpdateResultsDeferred();
    else
      owner()->RefreshAppsAndUpdateResults();
  }

 private:
  // AppIconDataSource::Delegate:
  std::unique_ptr<AppIconLoader> CreateIconLoader(
      int icon_size,
      AppIconLoaderDelegate* delegate) override {
    return std::make_unique<CrostiniAppIconLoader>(profile(), icon_size,
                                                   delegate);
  }

  DISALLOW_COPY_AND_ASSIGN(CrostiniDataSource);
};

}  // namespace

AppSearchProvider::AppSearchProvider(Profile* profile,
                                     AppListControllerDelegate* list_controller,
                                     base::Clock* clock,
                                     AppListModelUpdater* model_updater)
    : profile_(profile),
      list_controller_(list_controller),
      model_updater_(model_updater),
      clock_(clock) {
  bool app_service_enabled =
      base::FeatureList::IsEnabled(features::kAppServiceAsh);
  if (app_service_enabled) {
    data_sources_.emplace_back(
        std::make_unique<AppServiceDataSource>(profile, this));
  } else {
    data_sources_.emplace_back(
        std::make_unique<ExtensionDataSource>(profile, this));
    if (arc::IsArcAllowedForProfile(profile)) {
      data_sources_.emplace_back(
          std::make_unique<ArcDataSource>(profile, this));
    }
    if (crostini::IsCrostiniUIAllowedForProfile(profile)) {
      data_sources_.emplace_back(
          std::make_unique<CrostiniDataSource>(profile, this));
    }
  }
}

AppSearchProvider::~AppSearchProvider() {}

void AppSearchProvider::Start(const base::string16& query) {
  // When the AppSearchProvider initializes, UpdateRecommendedResults is called
  // three times. We only want to start updating user prefs for release notes
  // after these first three calls are done.
  query_ = query;
  query_start_time_ = base::TimeTicks::Now();
  // We only need to record app search latency for queries started by user.
  record_query_uma_ = true;
  const bool show_recommendations = query.empty();
  // Refresh list of apps to ensure we have the latest launch time information.
  // This will also cause the results to update.
  if (show_recommendations || apps_.empty())
    RefreshAppsAndUpdateResults();
  else
    UpdateResults();
}

void AppSearchProvider::ViewClosing() {
  ClearResultsSilently();
  for (auto& data_source : data_sources_)
    data_source->ViewClosing();
}

void AppSearchProvider::RefreshAppsAndUpdateResults() {
  // Clear any pending requests if any.
  refresh_apps_factory_.InvalidateWeakPtrs();

  apps_.clear();
  apps_.reserve(kMinimumReservedAppsContainerCapacity);
  for (auto& data_source : data_sources_)
    data_source->AddApps(&apps_);
  UpdateResults();
}

void AppSearchProvider::RefreshAppsAndUpdateResultsDeferred() {
  // Check if request is pending.
  if (refresh_apps_factory_.HasWeakPtrs())
    return;

  base::ThreadTaskRunnerHandle::Get()->PostTask(
      FROM_HERE, base::BindOnce(&AppSearchProvider::RefreshAppsAndUpdateResults,
                                refresh_apps_factory_.GetWeakPtr()));
}

void AppSearchProvider::UpdateRecommendedResults(
    const base::flat_map<std::string, uint16_t>& id_to_app_list_index) {
  SearchProvider::Results new_results;
  std::set<std::string> seen_or_filtered_apps;
  const uint16_t apps_size = apps_.size();
  new_results.reserve(apps_size);

  for (auto& app : apps_) {
    // Skip apps which cannot be shown as a suggested app.
    if (!app->recommendable())
      continue;

    base::string16 title = app->name();
    if (app->id() == ash::kInternalAppIdContinueReading) {
      base::string16 navigation_title;
      if (!HasRecommendableForeignTab(profile_, &navigation_title,
                                      /*url=*/nullptr,
                                      open_tabs_ui_delegate_for_testing())) {
        continue;
      } else if (!navigation_title.empty()) {
        title = navigation_title;
        app->AddSearchableText(title);
      }
    } else if (app->id() == ash::kReleaseNotesAppId) {
      auto release_notes_storage =
          std::make_unique<chromeos::ReleaseNotesStorage>(profile_);
      if (!release_notes_storage->ShouldShowSuggestionChip())
        continue;
    }

    std::unique_ptr<AppResult> result =
        app->data_source()->CreateResult(app->id(), list_controller_, true);
    result->SetTitle(title);

    const auto find_in_app_list = id_to_app_list_index.find(app->id());
    const base::Time time = app->GetLastActivityTime();

    // Set app->relevance based on the following criteria. Scores are set within
    // the range [0, 0.66], allowing the SearchResultRanker some headroom to set
    // higher rankings without having to re-range these scores.
    if (!time.is_null()) {
      // Case 1: if it has last activity time or install time, set the relevance
      // in [0.34, 0.66] based on the time.
      result->UpdateFromLastLaunchedOrInstalledTime(clock_->Now(), time);
      result->set_relevance(ReRange(result->relevance(), 0.34, 0.66));
    } else if (find_in_app_list != id_to_app_list_index.end()) {
      // Case 2: if it's in the app_list_index, set the relevance in [0.1, 0.33]
      result->set_relevance(
          ReRange(1.0f / (1.0f + find_in_app_list->second), 0.1, 0.33));
    } else {
      // Case 3: otherwise set the relevance as 0.0f;
      result->set_relevance(0.0f);
    }

    MaybeAddResult(&new_results, std::move(result), &seen_or_filtered_apps);
  }
  PublishQueriedResultsOrRecommendation(false, &new_results);
}

void AppSearchProvider::UpdateQueriedResults() {
  SearchProvider::Results new_results;
  std::set<std::string> seen_or_filtered_apps;
  const size_t apps_size = apps_.size();
  new_results.reserve(apps_size);

  const ash::TokenizedString query_terms(query_);
  for (auto& app : apps_) {
    if (!app->searchable())
      continue;

    ash::TokenizedString* indexed_name = app->GetTokenizedIndexedName();
    if (!app_list_features::IsFuzzyAppSearchEnabled()) {
      ash::TokenizedStringMatch match;
      if (match.Calculate(query_terms, *indexed_name)) {
        // Exact matches should be shown even if the threshold isn't reached,
        // e.g. due to a localized name being particularly short.
        if (match.relevance() <= app->relevance_threshold() &&
            !base::EqualsCaseInsensitiveASCII(query_, app->name()) &&
            !app->MatchSearchableText(query_terms)) {
          continue;
        }
      } else if (!app->MatchSearchableText(query_terms)) {
        continue;
      }
      std::unique_ptr<AppResult> result =
          app->data_source()->CreateResult(app->id(), list_controller_, false);
      result->UpdateFromMatch(*indexed_name, match);
      MaybeAddResult(&new_results, std::move(result), &seen_or_filtered_apps);
    } else {
      FuzzyTokenizedStringMatch match;
      if (match.IsRelevant(query_terms, *indexed_name) ||
          app->MatchSearchableText(query_terms) ||
          base::EqualsCaseInsensitiveASCII(query_, app->name())) {
        std::unique_ptr<AppResult> result = app->data_source()->CreateResult(
            app->id(), list_controller_, false);

        // Update result from match.
        result->SetTitle(indexed_name->text());
        result->set_relevance(match.relevance());
        ash::SearchResultTags tags;
        for (const auto& hit : match.hits()) {
          tags.push_back(ash::SearchResultTag(ash::SearchResultTag::MATCH,
                                              hit.start(), hit.end()));
        }
        result->SetTitleTags(tags);

        MaybeAddResult(&new_results, std::move(result), &seen_or_filtered_apps);
      }
    }
  }
  PublishQueriedResultsOrRecommendation(true, &new_results);
}

void AppSearchProvider::PublishQueriedResultsOrRecommendation(
    bool is_queried_search,
    Results* new_results) {
  MaybeRecordQueryLatencyHistogram(is_queried_search);
  SwapResults(new_results);
  update_results_factory_.InvalidateWeakPtrs();
}

void AppSearchProvider::MaybeRecordQueryLatencyHistogram(
    bool is_queried_search) {
  // Record the query latency only if search provider is queried by user
  // initiating a search or getting zero state suggestions.
  if (!record_query_uma_)
    return;

  if (is_queried_search) {
    UMA_HISTOGRAM_TIMES("Apps.AppList.AppSearchProvider.QueryTime",
                        base::TimeTicks::Now() - query_start_time_);
  } else {
    UMA_HISTOGRAM_TIMES("Apps.AppList.AppSearchProvider.ZeroStateLatency",
                        base::TimeTicks::Now() - query_start_time_);
  }
  record_query_uma_ = false;
}

void AppSearchProvider::UpdateResults() {
  const bool show_recommendations = query_.empty();

  // Presort app based on last activity time in order to be able to remove
  // duplicates from results. We break ties by App ID, which is arbitrary, but
  // deterministic.
  std::sort(apps_.begin(), apps_.end(),
            App::CompareByLastActivityTimeAndThenAppId());

  if (show_recommendations) {
    // Get the map of app ids to their position in the app list, and then
    // update results.
    model_updater_->GetIdToAppListIndexMap(
        base::BindOnce(&AppSearchProvider::UpdateRecommendedResults,
                       update_results_factory_.GetWeakPtr()));
  } else {
    UpdateQueriedResults();
  }
}

}  // namespace app_list
