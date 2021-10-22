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

#ifndef AKIT_FAILSAFE_FSROS_RAFT_STATE_HPP_
#define AKIT_FAILSAFE_FSROS_RAFT_STATE_HPP_

#include <functional>
#include <map>
#include <memory>
#include <string>

#include "raft/event/event.hpp"
#include "raft/event/event_observer.hpp"
#include "raft/state/state_type.hpp"

namespace akit {
namespace failsafe {
namespace fsros {

class State {
 public:
  State(StateType type, std::shared_ptr<EventObserver> observer,
        std::map<Event, StateType> transition_map);
  virtual ~State() {}

  StateType GetType();
  StateType Handle(const Event &event);
  void Emit(const Event &event);

  virtual void OnStarted() = 0;
  virtual void OnTimedout() = 0;
  virtual void OnVoteReceived() = 0;
  virtual void OnLeaderDiscovered() = 0;
  virtual void OnElected() = 0;
  virtual void OnTerminated() = 0;

  virtual void Entry() = 0;
  virtual void Exit() = 0;

 private:
  StateType type_;
  std::shared_ptr<EventObserver> event_observer_;
  std::map<Event, StateType> transition_map_;
};

}  // namespace fsros
}  // namespace failsafe
}  // namespace akit

#endif  // AKIT_FAILSAFE_FSROS_RAFT_STATE_HPP_
