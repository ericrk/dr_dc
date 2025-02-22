// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "services/network/host_resolver.h"

#include <map>
#include <utility>
#include <vector>

#include "base/logging.h"
#include "base/optional.h"
#include "base/run_loop.h"
#include "base/test/bind_test_util.h"
#include "base/test/simple_test_tick_clock.h"
#include "base/test/task_environment.h"
#include "mojo/public/cpp/bindings/binding.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "net/base/address_list.h"
#include "net/base/host_port_pair.h"
#include "net/base/ip_address.h"
#include "net/base/ip_endpoint.h"
#include "net/base/net_errors.h"
#include "net/dns/context_host_resolver.h"
#include "net/dns/dns_config.h"
#include "net/dns/dns_test_util.h"
#include "net/dns/host_resolver.h"
#include "net/dns/host_resolver_manager.h"
#include "net/dns/mock_host_resolver.h"
#include "net/dns/public/dns_protocol.h"
#include "net/log/net_log.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace network {
namespace {

class HostResolverTest : public testing::Test {
 public:
  HostResolverTest()
      : task_environment_(base::test::TaskEnvironment::MainThreadType::IO) {}

 protected:
  base::test::TaskEnvironment task_environment_;
};

net::IPEndPoint CreateExpectedEndPoint(const std::string& address,
                                       uint16_t port) {
  net::IPAddress ip_address;
  CHECK(ip_address.AssignFromIPLiteral(address));
  return net::IPEndPoint(ip_address, port);
}

class TestResolveHostClient : public mojom::ResolveHostClient {
 public:
  // If |run_loop| is non-null, will call RunLoop::Quit() on completion.
  TestResolveHostClient(mojo::PendingRemote<mojom::ResolveHostClient>* remote,
                        base::RunLoop* run_loop)
      : receiver_(this, remote->InitWithNewPipeAndPassReceiver()),
        complete_(false),
        result_error_(net::ERR_UNEXPECTED),
        run_loop_(run_loop) {}

  void CloseReceiver() { receiver_.reset(); }

  void OnComplete(int error,
                  const base::Optional<net::AddressList>& addresses) override {
    DCHECK(!complete_);

    complete_ = true;
    result_error_ = error;
    result_addresses_ = addresses;
    if (run_loop_)
      run_loop_->Quit();
  }

  void OnTextResults(const std::vector<std::string>& text_results) override {
    DCHECK(!complete_);
    result_text_ = text_results;
  }

  void OnHostnameResults(const std::vector<net::HostPortPair>& hosts) override {
    DCHECK(!complete_);
    result_hosts_ = hosts;
  }

  bool complete() const { return complete_; }

  int result_error() const {
    DCHECK(complete_);
    return result_error_;
  }

  const base::Optional<net::AddressList>& result_addresses() const {
    DCHECK(complete_);
    return result_addresses_;
  }

  const base::Optional<std::vector<std::string>>& result_text() const {
    DCHECK(complete_);
    return result_text_;
  }

  const base::Optional<std::vector<net::HostPortPair>>& result_hosts() const {
    DCHECK(complete_);
    return result_hosts_;
  }

 private:
  mojo::Receiver<mojom::ResolveHostClient> receiver_{this};

  bool complete_;
  int result_error_;
  base::Optional<net::AddressList> result_addresses_;
  base::Optional<std::vector<std::string>> result_text_;
  base::Optional<std::vector<net::HostPortPair>> result_hosts_;
  base::RunLoop* const run_loop_;
};

class TestMdnsListenClient : public mojom::MdnsListenClient {
 public:
  using UpdateType = net::HostResolver::MdnsListener::Delegate::UpdateType;
  using UpdateKey = std::pair<UpdateType, net::DnsQueryType>;

  explicit TestMdnsListenClient(mojom::MdnsListenClientPtr* interface_ptr)
      : binding_(this, mojo::MakeRequest(interface_ptr)) {}

  void OnAddressResult(UpdateType update_type,
                       net::DnsQueryType result_type,
                       const net::IPEndPoint& address) override {
    address_results_.insert({{update_type, result_type}, address});
  }

  void OnTextResult(UpdateType update_type,
                    net::DnsQueryType result_type,
                    const std::vector<std::string>& text_records) override {
    for (auto& text_record : text_records) {
      text_results_.insert({{update_type, result_type}, text_record});
    }
  }

  void OnHostnameResult(UpdateType update_type,
                        net::DnsQueryType result_type,
                        const net::HostPortPair& host) override {
    hostname_results_.insert({{update_type, result_type}, host});
  }

  void OnUnhandledResult(UpdateType update_type,
                         net::DnsQueryType result_type) override {
    unhandled_results_.insert({update_type, result_type});
  }

  const std::multimap<UpdateKey, net::IPEndPoint>& address_results() {
    return address_results_;
  }

  const std::multimap<UpdateKey, std::string>& text_results() {
    return text_results_;
  }

  const std::multimap<UpdateKey, net::HostPortPair>& hostname_results() {
    return hostname_results_;
  }

  const std::multiset<UpdateKey>& unhandled_results() {
    return unhandled_results_;
  }

  template <typename T>
  static std::pair<UpdateKey, T> CreateExpectedResult(
      UpdateType update_type,
      net::DnsQueryType query_type,
      T result) {
    return std::make_pair(std::make_pair(update_type, query_type), result);
  }

 private:
  mojo::Binding<mojom::MdnsListenClient> binding_;

  std::multimap<UpdateKey, net::IPEndPoint> address_results_;
  std::multimap<UpdateKey, std::string> text_results_;
  std::multimap<UpdateKey, net::HostPortPair> hostname_results_;
  std::multiset<UpdateKey> unhandled_results_;
};

TEST_F(HostResolverTest, Sync) {
  auto inner_resolver = std::make_unique<net::MockHostResolver>();
  inner_resolver->set_synchronous_mode(true);
  net::NetLog net_log;

  HostResolver resolver(inner_resolver.get(), &net_log);

  base::RunLoop run_loop;
  mojo::Remote<mojom::ResolveHostHandle> control_handle;
  mojom::ResolveHostParametersPtr optional_parameters =
      mojom::ResolveHostParameters::New();
  optional_parameters->control_handle =
      control_handle.BindNewPipeAndPassReceiver();
  mojo::PendingRemote<mojom::ResolveHostClient> pending_response_client;
  TestResolveHostClient response_client(&pending_response_client, &run_loop);

  resolver.ResolveHost(net::HostPortPair("localhost", 160),
                       std::move(optional_parameters),
                       std::move(pending_response_client));
  run_loop.Run();

  EXPECT_EQ(net::OK, response_client.result_error());
  EXPECT_THAT(response_client.result_addresses().value().endpoints(),
              testing::ElementsAre(CreateExpectedEndPoint("127.0.0.1", 160)));
  EXPECT_FALSE(response_client.result_text());
  EXPECT_FALSE(response_client.result_hosts());
  EXPECT_EQ(0u, resolver.GetNumOutstandingRequestsForTesting());
  EXPECT_EQ(net::DEFAULT_PRIORITY, inner_resolver->last_request_priority());
}

TEST_F(HostResolverTest, Async) {
  auto inner_resolver = std::make_unique<net::MockHostResolver>();
  inner_resolver->set_synchronous_mode(false);
  net::NetLog net_log;

  HostResolver resolver(inner_resolver.get(), &net_log);

  base::RunLoop run_loop;
  mojo::Remote<mojom::ResolveHostHandle> control_handle;
  mojom::ResolveHostParametersPtr optional_parameters =
      mojom::ResolveHostParameters::New();
  optional_parameters->control_handle =
      control_handle.BindNewPipeAndPassReceiver();
  mojo::PendingRemote<mojom::ResolveHostClient> pending_response_client;
  TestResolveHostClient response_client(&pending_response_client, &run_loop);

  resolver.ResolveHost(net::HostPortPair("localhost", 160),
                       std::move(optional_parameters),
                       std::move(pending_response_client));

  bool control_handle_closed = false;
  auto connection_error_callback =
      base::BindLambdaForTesting([&]() { control_handle_closed = true; });
  control_handle.set_disconnect_handler(connection_error_callback);
  run_loop.Run();

  EXPECT_EQ(net::OK, response_client.result_error());
  EXPECT_THAT(response_client.result_addresses().value().endpoints(),
              testing::ElementsAre(CreateExpectedEndPoint("127.0.0.1", 160)));
  EXPECT_FALSE(response_client.result_text());
  EXPECT_FALSE(response_client.result_hosts());
  EXPECT_TRUE(control_handle_closed);
  EXPECT_EQ(0u, resolver.GetNumOutstandingRequestsForTesting());
  EXPECT_EQ(net::DEFAULT_PRIORITY, inner_resolver->last_request_priority());
}

TEST_F(HostResolverTest, DnsQueryType) {
  net::NetLog net_log;
  std::unique_ptr<net::HostResolver> inner_resolver =
      net::HostResolver::CreateStandaloneResolver(&net_log);

  HostResolver resolver(inner_resolver.get(), &net_log);

  mojom::ResolveHostParametersPtr optional_parameters =
      mojom::ResolveHostParameters::New();
  optional_parameters->dns_query_type = net::DnsQueryType::AAAA;

  base::RunLoop run_loop;
  mojo::PendingRemote<mojom::ResolveHostClient> pending_response_client;
  TestResolveHostClient response_client(&pending_response_client, &run_loop);

  resolver.ResolveHost(net::HostPortPair("localhost", 160),
                       std::move(optional_parameters),
                       std::move(pending_response_client));
  run_loop.Run();

  EXPECT_EQ(net::OK, response_client.result_error());
  EXPECT_THAT(response_client.result_addresses().value().endpoints(),
              testing::ElementsAre(CreateExpectedEndPoint("::1", 160)));
}

TEST_F(HostResolverTest, InitialPriority) {
  auto inner_resolver = std::make_unique<net::MockHostResolver>();
  net::NetLog net_log;

  HostResolver resolver(inner_resolver.get(), &net_log);

  mojom::ResolveHostParametersPtr optional_parameters =
      mojom::ResolveHostParameters::New();
  optional_parameters->initial_priority = net::HIGHEST;

  base::RunLoop run_loop;
  mojo::PendingRemote<mojom::ResolveHostClient> pending_response_client;
  TestResolveHostClient response_client(&pending_response_client, &run_loop);

  resolver.ResolveHost(net::HostPortPair("localhost", 80),
                       std::move(optional_parameters),
                       std::move(pending_response_client));
  run_loop.Run();

  EXPECT_EQ(net::OK, response_client.result_error());
  EXPECT_THAT(response_client.result_addresses().value().endpoints(),
              testing::ElementsAre(CreateExpectedEndPoint("127.0.0.1", 80)));
  EXPECT_EQ(net::HIGHEST, inner_resolver->last_request_priority());
}

// Make requests specifying a source for host resolution and ensure the correct
// source is requested from the inner resolver.
TEST_F(HostResolverTest, Source) {
  constexpr char kDomain[] = "example.com";
  constexpr char kAnyResult[] = "1.2.3.4";
  constexpr char kSystemResult[] = "127.0.0.1";
  constexpr char kDnsResult[] = "168.100.12.23";
  constexpr char kMdnsResult[] = "200.1.2.3";
  auto inner_resolver = std::make_unique<net::MockHostResolver>();
  inner_resolver->rules_map()[net::HostResolverSource::ANY]->AddRule(
      kDomain, kAnyResult);
  inner_resolver->rules_map()[net::HostResolverSource::SYSTEM]->AddRule(
      kDomain, kSystemResult);
  inner_resolver->rules_map()[net::HostResolverSource::DNS]->AddRule(
      kDomain, kDnsResult);
  inner_resolver->rules_map()[net::HostResolverSource::MULTICAST_DNS]->AddRule(
      kDomain, kMdnsResult);

  net::NetLog net_log;
  HostResolver resolver(inner_resolver.get(), &net_log);

  base::RunLoop any_run_loop;
  mojo::PendingRemote<mojom::ResolveHostClient> pending_any_client;
  TestResolveHostClient any_client(&pending_any_client, &any_run_loop);
  mojom::ResolveHostParametersPtr any_parameters =
      mojom::ResolveHostParameters::New();
  any_parameters->source = net::HostResolverSource::ANY;
  resolver.ResolveHost(net::HostPortPair(kDomain, 80),
                       std::move(any_parameters),
                       std::move(pending_any_client));

  base::RunLoop system_run_loop;
  mojo::PendingRemote<mojom::ResolveHostClient> pending_system_client;
  TestResolveHostClient system_client(&pending_system_client, &system_run_loop);
  mojom::ResolveHostParametersPtr system_parameters =
      mojom::ResolveHostParameters::New();
  system_parameters->source = net::HostResolverSource::SYSTEM;
  resolver.ResolveHost(net::HostPortPair(kDomain, 80),
                       std::move(system_parameters),
                       std::move(pending_system_client));

  base::RunLoop dns_run_loop;
  mojo::PendingRemote<mojom::ResolveHostClient> pending_dns_client;
  TestResolveHostClient dns_client(&pending_dns_client, &dns_run_loop);
  mojom::ResolveHostParametersPtr dns_parameters =
      mojom::ResolveHostParameters::New();
  dns_parameters->source = net::HostResolverSource::DNS;
  resolver.ResolveHost(net::HostPortPair(kDomain, 80),
                       std::move(dns_parameters),
                       std::move(pending_dns_client));

  any_run_loop.Run();
  system_run_loop.Run();
  dns_run_loop.Run();

  EXPECT_EQ(net::OK, any_client.result_error());
  EXPECT_THAT(any_client.result_addresses().value().endpoints(),
              testing::ElementsAre(CreateExpectedEndPoint(kAnyResult, 80)));
  EXPECT_EQ(net::OK, system_client.result_error());
  EXPECT_THAT(system_client.result_addresses().value().endpoints(),
              testing::ElementsAre(CreateExpectedEndPoint(kSystemResult, 80)));
  EXPECT_EQ(net::OK, dns_client.result_error());
  EXPECT_THAT(dns_client.result_addresses().value().endpoints(),
              testing::ElementsAre(CreateExpectedEndPoint(kDnsResult, 80)));

#if BUILDFLAG(ENABLE_MDNS)
  base::RunLoop mdns_run_loop;
  mojo::PendingRemote<mojom::ResolveHostClient> pending_mdns_client;
  TestResolveHostClient mdns_client(&pending_mdns_client, &mdns_run_loop);
  mojom::ResolveHostParametersPtr mdns_parameters =
      mojom::ResolveHostParameters::New();
  mdns_parameters->source = net::HostResolverSource::MULTICAST_DNS;
  resolver.ResolveHost(net::HostPortPair(kDomain, 80),
                       std::move(mdns_parameters),
                       std::move(pending_mdns_client));

  mdns_run_loop.Run();

  EXPECT_EQ(net::OK, mdns_client.result_error());
  EXPECT_THAT(mdns_client.result_addresses().value().endpoints(),
              testing::ElementsAre(CreateExpectedEndPoint(kMdnsResult, 80)));
#endif  // BUILDFLAG(ENABLE_MDNS)
}

// Test that cached results are properly keyed by requested source.
TEST_F(HostResolverTest, SeparateCacheBySource) {
  constexpr char kDomain[] = "example.com";
  constexpr char kAnyResultOriginal[] = "1.2.3.4";
  constexpr char kSystemResultOriginal[] = "127.0.0.1";
  auto inner_resolver = std::make_unique<net::MockCachingHostResolver>();
  inner_resolver->rules_map()[net::HostResolverSource::ANY]->AddRule(
      kDomain, kAnyResultOriginal);
  inner_resolver->rules_map()[net::HostResolverSource::SYSTEM]->AddRule(
      kDomain, kSystemResultOriginal);
  base::SimpleTestTickClock test_clock;
  inner_resolver->set_tick_clock(&test_clock);

  net::NetLog net_log;
  HostResolver resolver(inner_resolver.get(), &net_log);

  // Load SYSTEM result into cache.
  base::RunLoop system_run_loop;
  mojo::PendingRemote<mojom::ResolveHostClient> pending_system_client_ptr;
  TestResolveHostClient system_client(&pending_system_client_ptr,
                                      &system_run_loop);
  mojom::ResolveHostParametersPtr system_parameters =
      mojom::ResolveHostParameters::New();
  system_parameters->source = net::HostResolverSource::SYSTEM;
  resolver.ResolveHost(net::HostPortPair(kDomain, 80),
                       std::move(system_parameters),
                       std::move(pending_system_client_ptr));
  system_run_loop.Run();
  ASSERT_EQ(net::OK, system_client.result_error());
  EXPECT_THAT(
      system_client.result_addresses().value().endpoints(),
      testing::ElementsAre(CreateExpectedEndPoint(kSystemResultOriginal, 80)));

  // Change |inner_resolver| rules to ensure results are coming from cache or
  // not based on whether they resolve to the old or new value.
  constexpr char kAnyResultFresh[] = "111.222.1.1";
  constexpr char kSystemResultFresh[] = "111.222.1.2";
  inner_resolver->rules_map()[net::HostResolverSource::ANY]->ClearRules();
  inner_resolver->rules_map()[net::HostResolverSource::ANY]->AddRule(
      kDomain, kAnyResultFresh);
  inner_resolver->rules_map()[net::HostResolverSource::SYSTEM]->ClearRules();
  inner_resolver->rules_map()[net::HostResolverSource::SYSTEM]->AddRule(
      kDomain, kSystemResultFresh);

  base::RunLoop cached_run_loop;
  mojo::PendingRemote<mojom::ResolveHostClient> pending_cached_client;
  TestResolveHostClient cached_client(&pending_cached_client, &cached_run_loop);
  mojom::ResolveHostParametersPtr cached_parameters =
      mojom::ResolveHostParameters::New();
  cached_parameters->source = net::HostResolverSource::SYSTEM;
  resolver.ResolveHost(net::HostPortPair(kDomain, 80),
                       std::move(cached_parameters),
                       std::move(pending_cached_client));

  base::RunLoop uncached_run_loop;
  mojo::PendingRemote<mojom::ResolveHostClient> pending_uncached_client;
  TestResolveHostClient uncached_client(&pending_uncached_client,
                                        &uncached_run_loop);
  mojom::ResolveHostParametersPtr uncached_parameters =
      mojom::ResolveHostParameters::New();
  uncached_parameters->source = net::HostResolverSource::ANY;
  resolver.ResolveHost(net::HostPortPair(kDomain, 80),
                       std::move(uncached_parameters),
                       std::move(pending_uncached_client));

  cached_run_loop.Run();
  uncached_run_loop.Run();

  EXPECT_EQ(net::OK, cached_client.result_error());
  EXPECT_THAT(
      cached_client.result_addresses().value().endpoints(),
      testing::ElementsAre(CreateExpectedEndPoint(kSystemResultOriginal, 80)));
  EXPECT_EQ(net::OK, uncached_client.result_error());
  EXPECT_THAT(
      uncached_client.result_addresses().value().endpoints(),
      testing::ElementsAre(CreateExpectedEndPoint(kAnyResultFresh, 80)));
}

TEST_F(HostResolverTest, CacheDisabled) {
  constexpr char kDomain[] = "example.com";
  constexpr char kResultOriginal[] = "1.2.3.4";
  auto inner_resolver = std::make_unique<net::MockCachingHostResolver>();
  inner_resolver->rules()->AddRule(kDomain, kResultOriginal);
  base::SimpleTestTickClock test_clock;
  inner_resolver->set_tick_clock(&test_clock);

  net::NetLog net_log;
  HostResolver resolver(inner_resolver.get(), &net_log);

  // Load result into cache.
  base::RunLoop run_loop;
  mojo::PendingRemote<mojom::ResolveHostClient> pending_client;
  TestResolveHostClient client(&pending_client, &run_loop);
  resolver.ResolveHost(net::HostPortPair(kDomain, 80), nullptr,
                       std::move(pending_client));
  run_loop.Run();
  ASSERT_EQ(net::OK, client.result_error());
  EXPECT_THAT(
      client.result_addresses().value().endpoints(),
      testing::ElementsAre(CreateExpectedEndPoint(kResultOriginal, 80)));

  // Change |inner_resolver| rules to ensure results are coming from cache or
  // not based on whether they resolve to the old or new value.
  constexpr char kResultFresh[] = "111.222.1.1";
  inner_resolver->rules()->ClearRules();
  inner_resolver->rules()->AddRule(kDomain, kResultFresh);

  base::RunLoop cached_run_loop;
  mojo::PendingRemote<mojom::ResolveHostClient> pending_cached_client;
  TestResolveHostClient cached_client(&pending_cached_client, &cached_run_loop);
  mojom::ResolveHostParametersPtr cached_parameters =
      mojom::ResolveHostParameters::New();
  cached_parameters->allow_cached_response = true;
  resolver.ResolveHost(net::HostPortPair(kDomain, 80),
                       std::move(cached_parameters),
                       std::move(pending_cached_client));
  cached_run_loop.Run();

  EXPECT_EQ(net::OK, cached_client.result_error());
  EXPECT_THAT(
      cached_client.result_addresses().value().endpoints(),
      testing::ElementsAre(CreateExpectedEndPoint(kResultOriginal, 80)));

  base::RunLoop uncached_run_loop;
  mojo::PendingRemote<mojom::ResolveHostClient> pending_uncached_client;
  TestResolveHostClient uncached_client(&pending_uncached_client,
                                        &uncached_run_loop);
  mojom::ResolveHostParametersPtr uncached_parameters =
      mojom::ResolveHostParameters::New();
  uncached_parameters->allow_cached_response = false;
  resolver.ResolveHost(net::HostPortPair(kDomain, 80),
                       std::move(uncached_parameters),
                       std::move(pending_uncached_client));
  uncached_run_loop.Run();

  EXPECT_EQ(net::OK, uncached_client.result_error());
  EXPECT_THAT(uncached_client.result_addresses().value().endpoints(),
              testing::ElementsAre(CreateExpectedEndPoint(kResultFresh, 80)));
}

// Test for a resolve with a result only in the cache and error if the cache is
// disabled.
TEST_F(HostResolverTest, CacheDisabled_ErrorResults) {
  constexpr char kDomain[] = "example.com";
  constexpr char kResult[] = "1.2.3.4";
  auto inner_resolver = std::make_unique<net::MockCachingHostResolver>();
  inner_resolver->rules()->AddRule(kDomain, kResult);
  base::SimpleTestTickClock test_clock;
  inner_resolver->set_tick_clock(&test_clock);

  net::NetLog net_log;
  HostResolver resolver(inner_resolver.get(), &net_log);

  // Load initial result into cache.
  base::RunLoop run_loop;
  mojo::PendingRemote<mojom::ResolveHostClient> pending_client;
  TestResolveHostClient client(&pending_client, &run_loop);
  resolver.ResolveHost(net::HostPortPair(kDomain, 80), nullptr,
                       std::move(pending_client));
  run_loop.Run();
  ASSERT_EQ(net::OK, client.result_error());

  // Change |inner_resolver| rules to an error.
  inner_resolver->rules()->ClearRules();
  inner_resolver->rules()->AddSimulatedFailure(kDomain);

  // Resolves for |kFreshErrorDomain| should result in error only when cache is
  // disabled because success was cached.
  base::RunLoop cached_run_loop;
  mojo::PendingRemote<mojom::ResolveHostClient> pending_cached_client;
  TestResolveHostClient cached_client(&pending_cached_client, &cached_run_loop);
  mojom::ResolveHostParametersPtr cached_parameters =
      mojom::ResolveHostParameters::New();
  cached_parameters->allow_cached_response = true;
  resolver.ResolveHost(net::HostPortPair(kDomain, 80),
                       std::move(cached_parameters),
                       std::move(pending_cached_client));
  cached_run_loop.Run();
  EXPECT_EQ(net::OK, cached_client.result_error());

  base::RunLoop uncached_run_loop;
  mojo::PendingRemote<mojom::ResolveHostClient> pending_uncached_client;
  TestResolveHostClient uncached_client(&pending_uncached_client,
                                        &uncached_run_loop);
  mojom::ResolveHostParametersPtr uncached_parameters =
      mojom::ResolveHostParameters::New();
  uncached_parameters->allow_cached_response = false;
  resolver.ResolveHost(net::HostPortPair(kDomain, 80),
                       std::move(uncached_parameters),
                       std::move(pending_uncached_client));
  uncached_run_loop.Run();
  EXPECT_EQ(net::ERR_NAME_NOT_RESOLVED, uncached_client.result_error());
}

TEST_F(HostResolverTest, IncludeCanonicalName) {
  auto inner_resolver = std::make_unique<net::MockHostResolver>();
  inner_resolver->rules()->AddRuleWithFlags("example.com", "123.0.12.24",
                                            net::HOST_RESOLVER_CANONNAME,
                                            "canonicalexample.com");
  net::NetLog net_log;

  HostResolver resolver(inner_resolver.get(), &net_log);

  mojom::ResolveHostParametersPtr optional_parameters =
      mojom::ResolveHostParameters::New();
  optional_parameters->include_canonical_name = true;

  base::RunLoop run_loop;
  mojo::PendingRemote<mojom::ResolveHostClient> pending_response_client;
  TestResolveHostClient response_client(&pending_response_client, &run_loop);

  resolver.ResolveHost(net::HostPortPair("example.com", 80),
                       std::move(optional_parameters),
                       std::move(pending_response_client));
  run_loop.Run();

  EXPECT_EQ(net::OK, response_client.result_error());
  EXPECT_THAT(response_client.result_addresses().value().endpoints(),
              testing::ElementsAre(CreateExpectedEndPoint("123.0.12.24", 80)));
  EXPECT_EQ("canonicalexample.com",
            response_client.result_addresses().value().canonical_name());
}

TEST_F(HostResolverTest, LoopbackOnly) {
  auto inner_resolver = std::make_unique<net::MockHostResolver>();
  inner_resolver->rules()->AddRuleWithFlags("example.com", "127.0.12.24",
                                            net::HOST_RESOLVER_LOOPBACK_ONLY);
  net::NetLog net_log;

  HostResolver resolver(inner_resolver.get(), &net_log);

  mojom::ResolveHostParametersPtr optional_parameters =
      mojom::ResolveHostParameters::New();
  optional_parameters->loopback_only = true;

  base::RunLoop run_loop;
  mojo::PendingRemote<mojom::ResolveHostClient> pending_response_client;
  TestResolveHostClient response_client(&pending_response_client, &run_loop);

  resolver.ResolveHost(net::HostPortPair("example.com", 80),
                       std::move(optional_parameters),
                       std::move(pending_response_client));
  run_loop.Run();

  EXPECT_EQ(net::OK, response_client.result_error());
  EXPECT_THAT(response_client.result_addresses().value().endpoints(),
              testing::ElementsAre(CreateExpectedEndPoint("127.0.12.24", 80)));
}

TEST_F(HostResolverTest, SecureDnsModeOverride) {
  auto inner_resolver = std::make_unique<net::MockHostResolver>();
  net::NetLog net_log;

  HostResolver resolver(inner_resolver.get(), &net_log);

  mojom::ResolveHostParametersPtr optional_parameters =
      mojom::ResolveHostParameters::New();
  optional_parameters->secure_dns_mode_override =
      network::mojom::OptionalSecureDnsMode::SECURE;

  base::RunLoop run_loop;
  mojo::PendingRemote<mojom::ResolveHostClient> pending_response_client;
  TestResolveHostClient response_client(&pending_response_client, &run_loop);

  resolver.ResolveHost(net::HostPortPair("localhost", 80),
                       std::move(optional_parameters),
                       std::move(pending_response_client));
  run_loop.Run();

  EXPECT_EQ(net::OK, response_client.result_error());
  EXPECT_THAT(response_client.result_addresses().value().endpoints(),
              testing::ElementsAre(CreateExpectedEndPoint("127.0.0.1", 80)));
  EXPECT_EQ(net::DnsConfig::SecureDnsMode::SECURE,
            inner_resolver->last_secure_dns_mode_override().value());
}

TEST_F(HostResolverTest, Failure_Sync) {
  auto inner_resolver = std::make_unique<net::MockHostResolver>();
  inner_resolver->rules()->AddSimulatedFailure("example.com");
  inner_resolver->set_synchronous_mode(true);
  net::NetLog net_log;

  HostResolver resolver(inner_resolver.get(), &net_log);

  base::RunLoop run_loop;
  mojo::Remote<mojom::ResolveHostHandle> control_handle;
  mojom::ResolveHostParametersPtr optional_parameters =
      mojom::ResolveHostParameters::New();
  optional_parameters->control_handle =
      control_handle.BindNewPipeAndPassReceiver();
  mojo::PendingRemote<mojom::ResolveHostClient> pending_response_client;
  TestResolveHostClient response_client(&pending_response_client, &run_loop);

  resolver.ResolveHost(net::HostPortPair("example.com", 160),
                       std::move(optional_parameters),
                       std::move(pending_response_client));
  run_loop.Run();

  EXPECT_EQ(net::ERR_NAME_NOT_RESOLVED, response_client.result_error());
  EXPECT_FALSE(response_client.result_addresses());
  EXPECT_EQ(0u, resolver.GetNumOutstandingRequestsForTesting());
}

TEST_F(HostResolverTest, Failure_Async) {
  auto inner_resolver = std::make_unique<net::MockHostResolver>();
  inner_resolver->rules()->AddSimulatedFailure("example.com");
  inner_resolver->set_synchronous_mode(false);
  net::NetLog net_log;

  HostResolver resolver(inner_resolver.get(), &net_log);

  base::RunLoop run_loop;
  mojo::Remote<mojom::ResolveHostHandle> control_handle;
  mojom::ResolveHostParametersPtr optional_parameters =
      mojom::ResolveHostParameters::New();
  optional_parameters->control_handle =
      control_handle.BindNewPipeAndPassReceiver();
  mojo::PendingRemote<mojom::ResolveHostClient> pending_response_client;
  TestResolveHostClient response_client(&pending_response_client, &run_loop);

  resolver.ResolveHost(net::HostPortPair("example.com", 160),
                       std::move(optional_parameters),
                       std::move(pending_response_client));

  bool control_handle_closed = false;
  auto connection_error_callback =
      base::BindLambdaForTesting([&]() { control_handle_closed = true; });
  control_handle.set_disconnect_handler(connection_error_callback);
  run_loop.Run();

  EXPECT_EQ(net::ERR_NAME_NOT_RESOLVED, response_client.result_error());
  EXPECT_FALSE(response_client.result_addresses());
  EXPECT_TRUE(control_handle_closed);
  EXPECT_EQ(0u, resolver.GetNumOutstandingRequestsForTesting());
}

TEST_F(HostResolverTest, NoOptionalParameters) {
  net::NetLog net_log;
  std::unique_ptr<net::HostResolver> inner_resolver =
      net::HostResolver::CreateStandaloneResolver(&net_log);

  HostResolver resolver(inner_resolver.get(), &net_log);

  base::RunLoop run_loop;
  mojo::PendingRemote<mojom::ResolveHostClient> pending_response_client;
  TestResolveHostClient response_client(&pending_response_client, &run_loop);

  // Resolve "localhost" because it should always resolve fast and locally, even
  // when using a real HostResolver.
  resolver.ResolveHost(net::HostPortPair("localhost", 80), nullptr,
                       std::move(pending_response_client));
  run_loop.Run();

  EXPECT_EQ(net::OK, response_client.result_error());
  EXPECT_THAT(
      response_client.result_addresses().value().endpoints(),
      testing::UnorderedElementsAre(CreateExpectedEndPoint("127.0.0.1", 80),
                                    CreateExpectedEndPoint("::1", 80)));
  EXPECT_EQ(0u, resolver.GetNumOutstandingRequestsForTesting());
}

TEST_F(HostResolverTest, NoControlHandle) {
  net::NetLog net_log;
  std::unique_ptr<net::HostResolver> inner_resolver =
      net::HostResolver::CreateStandaloneResolver(&net_log);

  HostResolver resolver(inner_resolver.get(), &net_log);

  base::RunLoop run_loop;
  mojom::ResolveHostParametersPtr optional_parameters =
      mojom::ResolveHostParameters::New();
  mojo::PendingRemote<mojom::ResolveHostClient> pending_response_client;
  TestResolveHostClient response_client(&pending_response_client, &run_loop);

  // Resolve "localhost" because it should always resolve fast and locally, even
  // when using a real HostResolver.
  resolver.ResolveHost(net::HostPortPair("localhost", 80),
                       std::move(optional_parameters),
                       std::move(pending_response_client));
  run_loop.Run();

  EXPECT_EQ(net::OK, response_client.result_error());
  EXPECT_THAT(
      response_client.result_addresses().value().endpoints(),
      testing::UnorderedElementsAre(CreateExpectedEndPoint("127.0.0.1", 80),
                                    CreateExpectedEndPoint("::1", 80)));
  EXPECT_EQ(0u, resolver.GetNumOutstandingRequestsForTesting());
}

TEST_F(HostResolverTest, CloseControlHandle) {
  net::NetLog net_log;
  std::unique_ptr<net::HostResolver> inner_resolver =
      net::HostResolver::CreateStandaloneResolver(&net_log);

  HostResolver resolver(inner_resolver.get(), &net_log);

  base::RunLoop run_loop;
  mojo::Remote<mojom::ResolveHostHandle> control_handle;
  mojom::ResolveHostParametersPtr optional_parameters =
      mojom::ResolveHostParameters::New();
  optional_parameters->control_handle =
      control_handle.BindNewPipeAndPassReceiver();
  mojo::PendingRemote<mojom::ResolveHostClient> pending_response_client;
  TestResolveHostClient response_client(&pending_response_client, &run_loop);

  // Resolve "localhost" because it should always resolve fast and locally, even
  // when using a real HostResolver.
  resolver.ResolveHost(net::HostPortPair("localhost", 160),
                       std::move(optional_parameters),
                       std::move(pending_response_client));
  control_handle.reset();
  run_loop.Run();

  EXPECT_EQ(net::OK, response_client.result_error());
  EXPECT_THAT(
      response_client.result_addresses().value().endpoints(),
      testing::UnorderedElementsAre(CreateExpectedEndPoint("127.0.0.1", 160),
                                    CreateExpectedEndPoint("::1", 160)));
  EXPECT_EQ(0u, resolver.GetNumOutstandingRequestsForTesting());
}

TEST_F(HostResolverTest, Cancellation) {
  // Use a HangingHostResolver, so the test can ensure the request won't be
  // completed before the cancellation arrives.
  auto inner_resolver = std::make_unique<net::HangingHostResolver>();
  net::NetLog net_log;

  HostResolver resolver(inner_resolver.get(), &net_log);

  ASSERT_EQ(0, inner_resolver->num_cancellations());

  base::RunLoop run_loop;
  mojo::Remote<mojom::ResolveHostHandle> control_handle;
  mojom::ResolveHostParametersPtr optional_parameters =
      mojom::ResolveHostParameters::New();
  optional_parameters->control_handle =
      control_handle.BindNewPipeAndPassReceiver();
  mojo::PendingRemote<mojom::ResolveHostClient> pending_response_client;
  TestResolveHostClient response_client(&pending_response_client, &run_loop);

  resolver.ResolveHost(net::HostPortPair("localhost", 80),
                       std::move(optional_parameters),
                       std::move(pending_response_client));
  bool control_handle_closed = false;
  auto connection_error_callback =
      base::BindLambdaForTesting([&]() { control_handle_closed = true; });
  control_handle.set_disconnect_handler(connection_error_callback);

  control_handle->Cancel(net::ERR_ABORTED);
  run_loop.Run();

  // On cancellation, should receive an ERR_FAILED result, and the internal
  // resolver request should have been cancelled.
  EXPECT_EQ(net::ERR_ABORTED, response_client.result_error());
  EXPECT_FALSE(response_client.result_addresses());
  EXPECT_EQ(1, inner_resolver->num_cancellations());
  EXPECT_TRUE(control_handle_closed);
  EXPECT_EQ(0u, resolver.GetNumOutstandingRequestsForTesting());
}

TEST_F(HostResolverTest, Cancellation_SubsequentRequest) {
  net::NetLog net_log;
  std::unique_ptr<net::HostResolver> inner_resolver =
      net::HostResolver::CreateStandaloneResolver(&net_log);

  HostResolver resolver(inner_resolver.get(), &net_log);

  base::RunLoop run_loop;
  mojo::Remote<mojom::ResolveHostHandle> control_handle;
  mojom::ResolveHostParametersPtr optional_parameters =
      mojom::ResolveHostParameters::New();
  optional_parameters->control_handle =
      control_handle.BindNewPipeAndPassReceiver();
  mojo::PendingRemote<mojom::ResolveHostClient> pending_response_client;
  TestResolveHostClient response_client(&pending_response_client, nullptr);

  resolver.ResolveHost(net::HostPortPair("localhost", 80),
                       std::move(optional_parameters),
                       std::move(pending_response_client));

  control_handle->Cancel(net::ERR_ABORTED);
  run_loop.RunUntilIdle();

  // Not using a hanging resolver, so could be ERR_ABORTED or OK depending on
  // timing of the cancellation.
  EXPECT_TRUE(response_client.result_error() == net::ERR_ABORTED ||
              response_client.result_error() == net::OK);
  EXPECT_EQ(0u, resolver.GetNumOutstandingRequestsForTesting());

  // Subsequent requests should be unaffected by the cancellation.
  base::RunLoop run_loop2;
  mojo::PendingRemote<mojom::ResolveHostClient> pending_response_client2;
  TestResolveHostClient response_client2(&pending_response_client2, &run_loop2);
  resolver.ResolveHost(net::HostPortPair("localhost", 80), nullptr,
                       std::move(pending_response_client2));
  run_loop2.Run();

  EXPECT_EQ(net::OK, response_client2.result_error());
  EXPECT_THAT(
      response_client2.result_addresses().value().endpoints(),
      testing::UnorderedElementsAre(CreateExpectedEndPoint("127.0.0.1", 80),
                                    CreateExpectedEndPoint("::1", 80)));
  EXPECT_EQ(0u, resolver.GetNumOutstandingRequestsForTesting());
}

TEST_F(HostResolverTest, DestroyResolver) {
  // Use a HangingHostResolver, so the test can ensure the request won't be
  // completed before the cancellation arrives.
  auto inner_resolver = std::make_unique<net::HangingHostResolver>();
  net::NetLog net_log;

  auto resolver =
      std::make_unique<HostResolver>(inner_resolver.get(), &net_log);

  ASSERT_EQ(0, inner_resolver->num_cancellations());

  base::RunLoop run_loop;
  mojo::Remote<mojom::ResolveHostHandle> control_handle;
  mojom::ResolveHostParametersPtr optional_parameters =
      mojom::ResolveHostParameters::New();
  optional_parameters->control_handle =
      control_handle.BindNewPipeAndPassReceiver();
  mojo::PendingRemote<mojom::ResolveHostClient> pending_response_client;
  TestResolveHostClient response_client(&pending_response_client, &run_loop);

  resolver->ResolveHost(net::HostPortPair("localhost", 80),
                        std::move(optional_parameters),
                        std::move(pending_response_client));
  bool control_handle_closed = false;
  auto connection_error_callback =
      base::BindLambdaForTesting([&]() { control_handle_closed = true; });
  control_handle.set_disconnect_handler(connection_error_callback);

  resolver = nullptr;
  run_loop.Run();

  // On context destruction, should receive an ERR_FAILED result, and the
  // internal resolver request should have been cancelled.
  EXPECT_EQ(net::ERR_FAILED, response_client.result_error());
  EXPECT_FALSE(response_client.result_addresses());
  EXPECT_EQ(1, inner_resolver->num_cancellations());
  EXPECT_TRUE(control_handle_closed);
}

TEST_F(HostResolverTest, CloseClient) {
  // Use a HangingHostResolver, so the test can ensure the request won't be
  // completed before the cancellation arrives.
  auto inner_resolver = std::make_unique<net::HangingHostResolver>();
  net::NetLog net_log;

  HostResolver resolver(inner_resolver.get(), &net_log);

  ASSERT_EQ(0, inner_resolver->num_cancellations());

  base::RunLoop run_loop;
  mojo::Remote<mojom::ResolveHostHandle> control_handle;
  mojom::ResolveHostParametersPtr optional_parameters =
      mojom::ResolveHostParameters::New();
  optional_parameters->control_handle =
      control_handle.BindNewPipeAndPassReceiver();
  mojo::PendingRemote<mojom::ResolveHostClient> pending_response_client;
  TestResolveHostClient response_client(&pending_response_client, &run_loop);

  resolver.ResolveHost(net::HostPortPair("localhost", 80),
                       std::move(optional_parameters),
                       std::move(pending_response_client));
  bool control_handle_closed = false;
  auto connection_error_callback =
      base::BindLambdaForTesting([&]() { control_handle_closed = true; });
  control_handle.set_disconnect_handler(connection_error_callback);

  response_client.CloseReceiver();
  run_loop.RunUntilIdle();

  // Response pipe is closed, so no results to check. Internal request should be
  // cancelled.
  EXPECT_FALSE(response_client.complete());
  EXPECT_EQ(1, inner_resolver->num_cancellations());
  EXPECT_TRUE(control_handle_closed);
  EXPECT_EQ(0u, resolver.GetNumOutstandingRequestsForTesting());
}

TEST_F(HostResolverTest, CloseClient_SubsequentRequest) {
  net::NetLog net_log;
  std::unique_ptr<net::HostResolver> inner_resolver =
      net::HostResolver::CreateStandaloneResolver(&net_log);

  HostResolver resolver(inner_resolver.get(), &net_log);

  base::RunLoop run_loop;
  mojo::PendingRemote<mojom::ResolveHostClient> pending_response_client;
  TestResolveHostClient response_client(&pending_response_client, nullptr);

  resolver.ResolveHost(net::HostPortPair("localhost", 80), nullptr,
                       std::move(pending_response_client));

  response_client.CloseReceiver();
  run_loop.RunUntilIdle();

  // Not using a hanging resolver, so could be incomplete or OK depending on
  // timing of the cancellation.
  EXPECT_TRUE(!response_client.complete() ||
              response_client.result_error() == net::OK);
  EXPECT_EQ(0u, resolver.GetNumOutstandingRequestsForTesting());

  // Subsequent requests should be unaffected by the cancellation.
  base::RunLoop run_loop2;
  mojo::PendingRemote<mojom::ResolveHostClient> pending_response_client2;
  TestResolveHostClient response_client2(&pending_response_client2, &run_loop2);
  resolver.ResolveHost(net::HostPortPair("localhost", 80), nullptr,
                       std::move(pending_response_client2));
  run_loop2.Run();

  EXPECT_EQ(net::OK, response_client2.result_error());
  EXPECT_THAT(
      response_client2.result_addresses().value().endpoints(),
      testing::UnorderedElementsAre(CreateExpectedEndPoint("127.0.0.1", 80),
                                    CreateExpectedEndPoint("::1", 80)));
  EXPECT_EQ(0u, resolver.GetNumOutstandingRequestsForTesting());
}

TEST_F(HostResolverTest, Binding) {
  mojo::Remote<mojom::HostResolver> resolver_remote;
  HostResolver* shutdown_resolver = nullptr;
  HostResolver::ConnectionShutdownCallback shutdown_callback =
      base::BindLambdaForTesting(
          [&](HostResolver* resolver) { shutdown_resolver = resolver; });

  net::NetLog net_log;
  std::unique_ptr<net::HostResolver> inner_resolver =
      net::HostResolver::CreateStandaloneResolver(&net_log);

  HostResolver resolver(resolver_remote.BindNewPipeAndPassReceiver(),
                        std::move(shutdown_callback), inner_resolver.get(),
                        &net_log);

  base::RunLoop run_loop;
  mojo::Remote<mojom::ResolveHostHandle> control_handle;
  mojom::ResolveHostParametersPtr optional_parameters =
      mojom::ResolveHostParameters::New();
  optional_parameters->control_handle =
      control_handle.BindNewPipeAndPassReceiver();
  mojo::PendingRemote<mojom::ResolveHostClient> pending_response_client;
  TestResolveHostClient response_client(&pending_response_client, &run_loop);

  // Resolve "localhost" because it should always resolve fast and locally, even
  // when using a real HostResolver.
  resolver_remote->ResolveHost(net::HostPortPair("localhost", 160),
                               std::move(optional_parameters),
                               std::move(pending_response_client));
  run_loop.Run();

  EXPECT_EQ(net::OK, response_client.result_error());
  EXPECT_THAT(
      response_client.result_addresses().value().endpoints(),
      testing::UnorderedElementsAre(CreateExpectedEndPoint("127.0.0.1", 160),
                                    CreateExpectedEndPoint("::1", 160)));
  EXPECT_EQ(0u, resolver.GetNumOutstandingRequestsForTesting());
  EXPECT_FALSE(shutdown_resolver);
}

TEST_F(HostResolverTest, CloseBinding) {
  mojo::Remote<mojom::HostResolver> resolver_remote;
  HostResolver* shutdown_resolver = nullptr;
  HostResolver::ConnectionShutdownCallback shutdown_callback =
      base::BindLambdaForTesting(
          [&](HostResolver* resolver) { shutdown_resolver = resolver; });

  // Use a HangingHostResolver, so the test can ensure the request won't be
  // completed before the cancellation arrives.
  auto inner_resolver = std::make_unique<net::HangingHostResolver>();
  net::NetLog net_log;

  HostResolver resolver(resolver_remote.BindNewPipeAndPassReceiver(),
                        std::move(shutdown_callback), inner_resolver.get(),
                        &net_log);

  ASSERT_EQ(0, inner_resolver->num_cancellations());

  base::RunLoop run_loop;
  mojo::Remote<mojom::ResolveHostHandle> control_handle;
  mojom::ResolveHostParametersPtr optional_parameters =
      mojom::ResolveHostParameters::New();
  optional_parameters->control_handle =
      control_handle.BindNewPipeAndPassReceiver();
  mojo::PendingRemote<mojom::ResolveHostClient> pending_response_client;
  TestResolveHostClient response_client(&pending_response_client, &run_loop);
  resolver_remote->ResolveHost(net::HostPortPair("localhost", 160),
                               std::move(optional_parameters),
                               std::move(pending_response_client));
  bool control_handle_closed = false;
  auto connection_error_callback =
      base::BindLambdaForTesting([&]() { control_handle_closed = true; });
  control_handle.set_disconnect_handler(connection_error_callback);

  resolver_remote.reset();
  run_loop.Run();

  // Request should be cancelled.
  EXPECT_EQ(net::ERR_FAILED, response_client.result_error());
  EXPECT_FALSE(response_client.result_addresses());
  EXPECT_TRUE(control_handle_closed);
  EXPECT_EQ(1, inner_resolver->num_cancellations());
  EXPECT_EQ(0u, resolver.GetNumOutstandingRequestsForTesting());

  // Callback should have been called.
  EXPECT_EQ(&resolver, shutdown_resolver);
}

TEST_F(HostResolverTest, CloseBinding_SubsequentRequest) {
  mojo::Remote<mojom::HostResolver> resolver_remote;
  HostResolver* shutdown_resolver = nullptr;
  HostResolver::ConnectionShutdownCallback shutdown_callback =
      base::BindLambdaForTesting(
          [&](HostResolver* resolver) { shutdown_resolver = resolver; });

  net::NetLog net_log;
  std::unique_ptr<net::HostResolver> inner_resolver =
      net::HostResolver::CreateStandaloneResolver(&net_log);

  HostResolver resolver(resolver_remote.BindNewPipeAndPassReceiver(),
                        std::move(shutdown_callback), inner_resolver.get(),
                        &net_log);

  base::RunLoop run_loop;
  mojo::PendingRemote<mojom::ResolveHostClient> pending_response_client;
  TestResolveHostClient response_client(&pending_response_client, nullptr);
  resolver_remote->ResolveHost(net::HostPortPair("localhost", 160), nullptr,
                               std::move(pending_response_client));

  resolver_remote.reset();
  run_loop.RunUntilIdle();

  // Not using a hanging resolver, so could be ERR_FAILED or OK depending on
  // timing of the cancellation.
  EXPECT_TRUE(response_client.result_error() == net::ERR_FAILED ||
              response_client.result_error() == net::OK);
  EXPECT_EQ(0u, resolver.GetNumOutstandingRequestsForTesting());

  // Callback should have been called.
  EXPECT_EQ(&resolver, shutdown_resolver);

  // Subsequent requests should be unaffected by the cancellation.
  base::RunLoop run_loop2;
  mojo::PendingRemote<mojom::ResolveHostClient> pending_response_client2;
  TestResolveHostClient response_client2(&pending_response_client2, &run_loop2);
  resolver.ResolveHost(net::HostPortPair("localhost", 80), nullptr,
                       std::move(pending_response_client2));
  run_loop2.Run();

  EXPECT_EQ(net::OK, response_client2.result_error());
  EXPECT_THAT(
      response_client2.result_addresses().value().endpoints(),
      testing::UnorderedElementsAre(CreateExpectedEndPoint("127.0.0.1", 80),
                                    CreateExpectedEndPoint("::1", 80)));
  EXPECT_EQ(0u, resolver.GetNumOutstandingRequestsForTesting());
}

TEST_F(HostResolverTest, IsSpeculative) {
  net::NetLog net_log;
  std::unique_ptr<net::HostResolver> inner_resolver =
      net::HostResolver::CreateStandaloneResolver(&net_log);

  HostResolver resolver(inner_resolver.get(), &net_log);

  base::RunLoop run_loop;
  mojo::PendingRemote<mojom::ResolveHostClient> pending_response_client;
  TestResolveHostClient response_client(&pending_response_client, &run_loop);
  mojom::ResolveHostParametersPtr parameters =
      mojom::ResolveHostParameters::New();
  parameters->is_speculative = true;

  // Resolve "localhost" because it should always resolve fast and locally, even
  // when using a real HostResolver.
  resolver.ResolveHost(net::HostPortPair("localhost", 80),
                       std::move(parameters),
                       std::move(pending_response_client));
  run_loop.Run();

  EXPECT_EQ(net::OK, response_client.result_error());
  EXPECT_FALSE(response_client.result_addresses());
  EXPECT_EQ(0u, resolver.GetNumOutstandingRequestsForTesting());
}

net::DnsConfig CreateValidDnsConfig() {
  net::IPAddress dns_ip(192, 168, 1, 0);
  net::DnsConfig config;
  config.nameservers.push_back(
      net::IPEndPoint(dns_ip, net::dns_protocol::kDefaultPort));
  EXPECT_TRUE(config.IsValid());
  return config;
}

TEST_F(HostResolverTest, TextResults) {
  static const char* kTextRecords[] = {"foo", "bar", "more text"};
  net::MockDnsClientRuleList rules;
  rules.emplace_back(
      "example.com", net::dns_protocol::kTypeTXT, false /* secure */,
      net::MockDnsClientRule::Result(net::BuildTestDnsTextResponse(
          "example.com", {std::vector<std::string>(std::begin(kTextRecords),
                                                   std::end(kTextRecords))})),
      false /* delay */);
  auto dns_client = std::make_unique<net::MockDnsClient>(CreateValidDnsConfig(),
                                                         std::move(rules));
  dns_client->set_ignore_system_config_changes(true);

  net::NetLog net_log;
  std::unique_ptr<net::ContextHostResolver> inner_resolver =
      net::HostResolver::CreateStandaloneContextResolver(&net_log);
  inner_resolver->GetManagerForTesting()->SetDnsClientForTesting(
      std::move(dns_client));
  inner_resolver->GetManagerForTesting()->SetInsecureDnsClientEnabled(true);

  HostResolver resolver(inner_resolver.get(), &net_log);

  base::RunLoop run_loop;
  mojom::ResolveHostParametersPtr optional_parameters =
      mojom::ResolveHostParameters::New();
  optional_parameters->dns_query_type = net::DnsQueryType::TXT;
  mojo::PendingRemote<mojom::ResolveHostClient> pending_response_client;
  TestResolveHostClient response_client(&pending_response_client, &run_loop);

  resolver.ResolveHost(net::HostPortPair("example.com", 160),
                       std::move(optional_parameters),
                       std::move(pending_response_client));
  run_loop.Run();

  EXPECT_EQ(net::OK, response_client.result_error());
  EXPECT_FALSE(response_client.result_addresses());
  EXPECT_THAT(response_client.result_text(),
              testing::Optional(testing::ElementsAreArray(kTextRecords)));
  EXPECT_FALSE(response_client.result_hosts());
  EXPECT_EQ(0u, resolver.GetNumOutstandingRequestsForTesting());
}

TEST_F(HostResolverTest, HostResults) {
  net::MockDnsClientRuleList rules;
  rules.emplace_back(
      "example.com", net::dns_protocol::kTypePTR, false /*secure */,
      net::MockDnsClientRule::Result(net::BuildTestDnsPointerResponse(
          "example.com", {"google.com", "chromium.org"})),
      false /* delay */);
  auto dns_client = std::make_unique<net::MockDnsClient>(CreateValidDnsConfig(),
                                                         std::move(rules));
  dns_client->set_ignore_system_config_changes(true);

  net::NetLog net_log;
  std::unique_ptr<net::ContextHostResolver> inner_resolver =
      net::HostResolver::CreateStandaloneContextResolver(&net_log);
  inner_resolver->GetManagerForTesting()->SetDnsClientForTesting(
      std::move(dns_client));
  inner_resolver->GetManagerForTesting()->SetInsecureDnsClientEnabled(true);

  HostResolver resolver(inner_resolver.get(), &net_log);

  base::RunLoop run_loop;
  mojom::ResolveHostParametersPtr optional_parameters =
      mojom::ResolveHostParameters::New();
  optional_parameters->dns_query_type = net::DnsQueryType::PTR;
  mojo::PendingRemote<mojom::ResolveHostClient> pending_response_client;
  TestResolveHostClient response_client(&pending_response_client, &run_loop);

  resolver.ResolveHost(net::HostPortPair("example.com", 160),
                       std::move(optional_parameters),
                       std::move(pending_response_client));
  run_loop.Run();

  EXPECT_EQ(net::OK, response_client.result_error());
  EXPECT_FALSE(response_client.result_addresses());
  EXPECT_FALSE(response_client.result_text());
  EXPECT_THAT(response_client.result_hosts(),
              testing::Optional(testing::UnorderedElementsAre(
                  net::HostPortPair("google.com", 160),
                  net::HostPortPair("chromium.org", 160))));
  EXPECT_EQ(0u, resolver.GetNumOutstandingRequestsForTesting());
}

#if BUILDFLAG(ENABLE_MDNS)
TEST_F(HostResolverTest, MdnsListener_AddressResult) {
  net::NetLog net_log;
  auto inner_resolver = std::make_unique<net::MockHostResolver>();
  HostResolver resolver(inner_resolver.get(), &net_log);

  mojom::MdnsListenClientPtr pending_response_client;
  TestMdnsListenClient response_client(&pending_response_client);

  int error = net::ERR_FAILED;
  base::RunLoop run_loop;
  net::HostPortPair host("host.local", 41);
  resolver.MdnsListen(host, net::DnsQueryType::A,
                      std::move(pending_response_client),
                      base::BindLambdaForTesting([&](int error_val) {
                        error = error_val;
                        run_loop.Quit();
                      }));

  run_loop.Run();
  ASSERT_EQ(net::OK, error);

  net::IPAddress result_address(1, 2, 3, 4);
  net::IPEndPoint result(result_address, 41);
  inner_resolver->TriggerMdnsListeners(
      host, net::DnsQueryType::A,
      net::HostResolver::MdnsListener::Delegate::UpdateType::ADDED, result);
  base::RunLoop().RunUntilIdle();

  EXPECT_THAT(response_client.address_results(),
              testing::ElementsAre(TestMdnsListenClient::CreateExpectedResult(
                  net::HostResolver::MdnsListener::Delegate::UpdateType::ADDED,
                  net::DnsQueryType::A, result)));

  EXPECT_THAT(response_client.text_results(), testing::IsEmpty());
  EXPECT_THAT(response_client.hostname_results(), testing::IsEmpty());
  EXPECT_THAT(response_client.unhandled_results(), testing::IsEmpty());
}

TEST_F(HostResolverTest, MdnsListener_TextResult) {
  net::NetLog net_log;
  auto inner_resolver = std::make_unique<net::MockHostResolver>();
  HostResolver resolver(inner_resolver.get(), &net_log);

  mojom::MdnsListenClientPtr pending_response_client;
  TestMdnsListenClient response_client(&pending_response_client);

  int error = net::ERR_FAILED;
  base::RunLoop run_loop;
  net::HostPortPair host("host.local", 42);
  resolver.MdnsListen(host, net::DnsQueryType::TXT,
                      std::move(pending_response_client),
                      base::BindLambdaForTesting([&](int error_val) {
                        error = error_val;
                        run_loop.Quit();
                      }));

  run_loop.Run();
  ASSERT_EQ(net::OK, error);

  inner_resolver->TriggerMdnsListeners(
      host, net::DnsQueryType::TXT,
      net::HostResolver::MdnsListener::Delegate::UpdateType::CHANGED,
      {"foo", "bar"});
  base::RunLoop().RunUntilIdle();

  EXPECT_THAT(
      response_client.text_results(),
      testing::UnorderedElementsAre(
          TestMdnsListenClient::CreateExpectedResult(
              net::HostResolver::MdnsListener::Delegate::UpdateType::CHANGED,
              net::DnsQueryType::TXT, "foo"),
          TestMdnsListenClient::CreateExpectedResult(
              net::HostResolver::MdnsListener::Delegate::UpdateType::CHANGED,
              net::DnsQueryType::TXT, "bar")));

  EXPECT_THAT(response_client.address_results(), testing::IsEmpty());
  EXPECT_THAT(response_client.hostname_results(), testing::IsEmpty());
  EXPECT_THAT(response_client.unhandled_results(), testing::IsEmpty());
}

TEST_F(HostResolverTest, MdnsListener_HostnameResult) {
  net::NetLog net_log;
  auto inner_resolver = std::make_unique<net::MockHostResolver>();
  HostResolver resolver(inner_resolver.get(), &net_log);

  mojom::MdnsListenClientPtr pending_response_client;
  TestMdnsListenClient response_client(&pending_response_client);

  int error = net::ERR_FAILED;
  base::RunLoop run_loop;
  net::HostPortPair host("host.local", 43);
  resolver.MdnsListen(host, net::DnsQueryType::PTR,
                      std::move(pending_response_client),
                      base::BindLambdaForTesting([&](int error_val) {
                        error = error_val;
                        run_loop.Quit();
                      }));

  run_loop.Run();
  ASSERT_EQ(net::OK, error);

  net::HostPortPair result("example.com", 43);
  inner_resolver->TriggerMdnsListeners(
      host, net::DnsQueryType::PTR,
      net::HostResolver::MdnsListener::Delegate::UpdateType::REMOVED, result);
  base::RunLoop().RunUntilIdle();

  EXPECT_THAT(
      response_client.hostname_results(),
      testing::ElementsAre(TestMdnsListenClient::CreateExpectedResult(
          net::HostResolver::MdnsListener::Delegate::UpdateType::REMOVED,
          net::DnsQueryType::PTR, result)));

  EXPECT_THAT(response_client.address_results(), testing::IsEmpty());
  EXPECT_THAT(response_client.text_results(), testing::IsEmpty());
  EXPECT_THAT(response_client.unhandled_results(), testing::IsEmpty());
}

TEST_F(HostResolverTest, MdnsListener_UnhandledResult) {
  net::NetLog net_log;
  auto inner_resolver = std::make_unique<net::MockHostResolver>();
  HostResolver resolver(inner_resolver.get(), &net_log);

  mojom::MdnsListenClientPtr pending_response_client;
  TestMdnsListenClient response_client(&pending_response_client);

  int error = net::ERR_FAILED;
  base::RunLoop run_loop;
  net::HostPortPair host("host.local", 44);
  resolver.MdnsListen(host, net::DnsQueryType::PTR,
                      std::move(pending_response_client),
                      base::BindLambdaForTesting([&](int error_val) {
                        error = error_val;
                        run_loop.Quit();
                      }));

  run_loop.Run();
  ASSERT_EQ(net::OK, error);

  inner_resolver->TriggerMdnsListeners(
      host, net::DnsQueryType::PTR,
      net::HostResolver::MdnsListener::Delegate::UpdateType::ADDED);
  base::RunLoop().RunUntilIdle();

  EXPECT_THAT(response_client.unhandled_results(),
              testing::ElementsAre(std::make_pair(
                  net::HostResolver::MdnsListener::Delegate::UpdateType::ADDED,
                  net::DnsQueryType::PTR)));

  EXPECT_THAT(response_client.address_results(), testing::IsEmpty());
  EXPECT_THAT(response_client.text_results(), testing::IsEmpty());
  EXPECT_THAT(response_client.hostname_results(), testing::IsEmpty());
}
#endif  // BUILDFLAG(ENABLE_MDNS)

}  // namespace
}  // namespace network
