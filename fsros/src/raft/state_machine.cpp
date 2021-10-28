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

#include "raft/state_machine.hpp"

#include <rclcpp/create_client.hpp>

#include <iostream>
#include <memory>
#include <string>
#include <tuple>
#include <vector>

#include "common/node_util.hpp"
#include "fsros_msgs/srv/append_entries.hpp"
#include "raft/context.hpp"

namespace akit {
namespace failsafe {
namespace fsros {
namespace raft {

StateMachine::StateMachine(const std::vector<uint32_t> &cluster_node_ids,
                           std::shared_ptr<Context> context)
    : common::StateMachine<State, StateType, Event>(
          StateType::kStandby,
          {{StateType::kStandby, std::make_shared<Standby>(context)},
           {StateType::kFollower, std::make_shared<Follower>(context)},
           {StateType::kCandidate, std::make_shared<Candidate>(context)},
           {StateType::kLeader, std::make_shared<Leader>(context)}}),
      context_(context) {
  initialize_services();
  initialize_clients(cluster_node_ids);
  context_->add_election_timer_callback(
      std::bind(&StateMachine::on_election_timedout, this));
}

void StateMachine::initialize_services() {
  rcl_service_options_t options = rcl_service_get_default_options();

  context_->append_entries_callback_.set(std::bind(
      &StateMachine::on_append_entries_requested, this, std::placeholders::_1,
      std::placeholders::_2, std::placeholders::_3));

  context_->append_entries_service_ =
      std::make_shared<rclcpp::Service<fsros_msgs::srv::AppendEntries>>(
          context_->node_base_->get_shared_rcl_node_handle(),
          NodeUtil::get_service_name(context_->node_base_->get_namespace(),
                                     context_->node_id_,
                                     kAppendEntriesServiceName),
          context_->append_entries_callback_, options);
  context_->node_services_->add_service(
      std::dynamic_pointer_cast<rclcpp::ServiceBase>(
          context_->append_entries_service_),
      nullptr);

  context_->request_vote_callback_.set(std::bind(
      &StateMachine::on_request_vote_requested, this, std::placeholders::_1,
      std::placeholders::_2, std::placeholders::_3));

  context_->request_vote_service_ =
      std::make_shared<rclcpp::Service<fsros_msgs::srv::RequestVote>>(
          context_->node_base_->get_shared_rcl_node_handle(),
          NodeUtil::get_service_name(context_->node_base_->get_namespace(),
                                     context_->node_id_,
                                     kRequestVoteServiceName),
          context_->request_vote_callback_, options);
  context_->node_services_->add_service(
      std::dynamic_pointer_cast<rclcpp::ServiceBase>(
          context_->request_vote_service_),
      nullptr);
}

void StateMachine::initialize_clients(
    const std::vector<uint32_t> &cluster_node_ids) {
  std::string cluster_name = context_->node_base_->get_namespace();
  rcl_client_options_t options = rcl_client_get_default_options();
  options.qos = rmw_qos_profile_services_default;

  for (auto id : cluster_node_ids) {
    if (id == context_->node_id_) {
      continue;
    }

    auto append_entries =
        rclcpp::Client<fsros_msgs::srv::AppendEntries>::make_shared(
            context_->node_base_.get(), context_->node_graph_,
            NodeUtil::get_service_name(context_->node_base_->get_namespace(),
                                       id, kAppendEntriesServiceName),
            options);
    context_->node_services_->add_client(
        std::dynamic_pointer_cast<rclcpp::ClientBase>(append_entries), nullptr);
    context_->append_entries_clients_.push_back(append_entries);

    auto request_vote =
        rclcpp::Client<fsros_msgs::srv::RequestVote>::make_shared(
            context_->node_base_.get(), context_->node_graph_,
            NodeUtil::get_service_name(context_->node_base_->get_namespace(),
                                       id, kRequestVoteServiceName),
            options);
    context_->node_services_->add_client(
        std::dynamic_pointer_cast<rclcpp::ClientBase>(request_vote), nullptr);
    context_->request_vote_clients_.push_back(request_vote);
  }
}

void StateMachine::on_append_entries_requested(
    const std::shared_ptr<rmw_request_id_t>,
    const std::shared_ptr<fsros_msgs::srv::AppendEntries::Request> request,
    std::shared_ptr<fsros_msgs::srv::AppendEntries::Response> response) {
  auto state = get_current_state();
  if (state == nullptr) {
    std::cerr << "There is no current state" << std::endl;
    return;
  }

  std::tie(response->term, response->success) =
      state->on_append_entries_received(request->term);
}

void StateMachine::on_request_vote_requested(
    const std::shared_ptr<rmw_request_id_t>,
    const std::shared_ptr<fsros_msgs::srv::RequestVote::Request> request,
    std::shared_ptr<fsros_msgs::srv::RequestVote::Response> response) {
  auto state = get_current_state();
  if (state == nullptr) {
    std::cerr << "There is no current state" << std::endl;
    return;
  }

  std::tie(response->term, response->vote_granted) =
      state->on_request_vote_received(request->term, request->candidate_id);
}

void StateMachine::on_election_timedout() {
  std::cout << "[" << context_->node_base_->get_name() << ": State("
            << static_cast<int>(get_current_state_type())
            << ")] on_election_timedout" << std::endl;
  handle(Event::kTimedout);
}

}  // namespace raft
}  // namespace fsros
}  // namespace failsafe
}  // namespace akit
