/*
 * Copyright (c) 2021 42dot All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <chrono>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "akit/failover/foros/cluster_node.hpp"
#include "akit/failover/foros/cluster_node_options.hpp"
#include "rclcpp/rclcpp.hpp"

using namespace std::chrono_literals;

class MyDataInterface : public akit::failover::foros::ClusterNodeDataInterface {
 public:
  explicit MyDataInterface(
      std::map<uint64_t, akit::failover::foros::Data::SharedPtr> &dataset,
      uint32_t &data_cnt)
      : dataset_(dataset), data_cnt_(data_cnt), changed_(true) {}

  akit::failover::foros::Data::SharedPtr on_data_get_requested(
      uint64_t id) override {
    if (id >= data_cnt_) {
      return nullptr;
    }
    dump();

    return dataset_[id];
  }

  akit::failover::foros::Data::SharedPtr on_data_get_requested() override {
    if (data_cnt_ == 0) {
      return nullptr;
    }
    dump();
    return dataset_[data_cnt_ - 1];
  }

  void on_data_rollback_requested(uint64_t id) override {
    std::cout << "rollback requested to " << id << std::endl;
    data_cnt_ = id;
    changed_ = true;
    dump();
  }

  bool on_data_commit_requested(akit::failover::foros::Data::SharedPtr data) {
    if (data->id() != data_cnt_) {
      std::cerr << "Invalid data commit requested: " << data->id()
                << " (latest: " << data_cnt_ << ")" << std::endl;
      return false;
    }

    dataset_[data_cnt_++] = data;
    std::cout << "data commited to " << data_cnt_ - 1 << std::endl;
    changed_ = true;
    dump();
    return true;
  }

  void dump() {
    if (changed_ == false) {
      return;
    }
    changed_ = false;
    std::cout << "===== data dump =====" << std::endl;
    for (uint32_t i = 0; i < data_cnt_; i++) {
      std::cout << dataset_[i]->id() << ": "
                << std::string(1, dataset_[i]->data()[0]) << std::endl;
    }
    std::cout << "=====================" << std::endl;
  }

  void commit(akit::failover::foros::Data::SharedPtr data) {
    if (data->id() != data_cnt_) {
      std::cerr << "Invalid data commit (by consensus): " << data->id()
                << " (latest: " << data_cnt_ << ")" << std::endl;
      return;
    }
    changed_ = true;
    dataset_[data_cnt_++] = data;
    dump();
  }

 private:
  std::map<uint64_t, akit::failover::foros::Data::SharedPtr> &dataset_;
  uint32_t &data_cnt_;
  bool changed_;
};

int main(int argc, char **argv) {
  const std::string kClusterName = "test_cluster";
  const std::vector<uint32_t> kClusterNodeIds = {1, 2, 3, 4};
  std::map<uint64_t, akit::failover::foros::Data::SharedPtr> dataset;

  if (argc < 3) {
    std::cerr << "Usage: " << argv[0]
              << " {node ID out of 1, 2, 3, 4} {number of data}" << std::endl;
    return -1;
  }

  uint32_t id = std::stoul(argv[1]);
  if (id > 4 || id == 0) {
    std::cerr << "please use id out of 1, 2, 3, 4" << std::endl;
  }

  uint32_t data_cnt = std::stoul(argv[2]);

  unsigned char ch = 'a';
  for (uint32_t i = 0; i < data_cnt; i++) {
    auto data = akit::failover::foros::Data::make_shared(
        i, 0, std::initializer_list<uint8_t>{ch++});
    dataset[i] = data;
  }

  rclcpp::init(argc, argv);

  auto options = akit::failover::foros::ClusterNodeOptions();
  options.election_timeout_max(2000);
  options.election_timeout_min(1500);

  auto data_interface = std::make_shared<MyDataInterface>(dataset, data_cnt);
  auto node = akit::failover::foros::ClusterNode::make_shared(
      kClusterName, id, kClusterNodeIds, data_interface, options);

  node->register_on_activated([]() { std::cout << "activated" << std::endl; });
  node->register_on_deactivated(
      []() { std::cout << "deactivated" << std::endl; });
  node->register_on_standby([]() { std::cout << "standby" << std::endl; });

  auto timer_ =
      rclcpp::create_timer(node, rclcpp::Clock::make_shared(), 2s, [&]() {
        node->commit_data(
            data_cnt, std::initializer_list<uint8_t>{ch},
            [&](akit::failover::foros::DataCommitResponseSharedFuture
                    response_future) {
              auto response = response_future.get();
              if (response->result_ == true) {
                data_interface->commit(response->data_);
                ch++;
              }
            });
      });

  rclcpp::spin(node->get_node_base_interface());
  rclcpp::shutdown();

  return 0;
}