// Copyright (C) 2020 The DMLab2D Authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// Room represents the topology (wall, floor and targets) of a room as
// well as its contained player and boxes.
//
// This implementation also allows to apply actions (player movement) to the
// room. However, given that its intended use is the procedural generation of
// Pushbox levels, the actions that can be applied are inverted (pulling instead
// of pushing boxes). The reason is that valid levels are generated by applying
// a number of 'inverse' actions, guaranteeing that the obtained levels are
// valid.

#ifndef DMLAB2D_LIB_SYSTEM_GENERATORS_PUSHBOX_ROOM_H_
#define DMLAB2D_LIB_SYSTEM_GENERATORS_PUSHBOX_ROOM_H_

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#include "absl/types/span.h"
#include "dmlab2d/lib/system/math/math2d.h"

namespace deepmind::lab2d::pushbox {

// An action indicates a direction and whether we are pulling a box.
struct Action {
  math::Vector2d direction;
  bool pull;
};

// Represents a possible entity to be placed in the room in a non fixed location
// and that can be subject to actions/or be moved. There are two types of
// entities: the player and boxes.
// Each entity stores its initial and current position which will be used for
// calculating room generation statistics.
// Note: This class is not intended to be directly instantiated and is used as
// base class for the Box and Player classes.
class Entity {
 public:
  explicit Entity(const math::Vector2d& position)
      : initial_position_(position), position_(position) {}

  // Set/Get the position of the entity.
  void set_position(const math::Vector2d& position) { position_ = position; }
  const math::Vector2d& position() const { return position_; }

  // Displacement from the original position.
  float Displacement() const {
    // Return Manhattan distance.
    return std::abs(initial_position_.x - position_.x) +
           std::abs(initial_position_.y - position_.y);
  }

 protected:
  // Protected constructor to avoid instantiation of this class (use Box or
  // Player classes instead).
  ~Entity() = default;

 private:
  // Initial location for this entity.
  math::Vector2d initial_position_;

  // Current position.
  math::Vector2d position_;
};

// This class represent a player as a specific type of Entity that can be
// subject to actions (move around, pulls boxes...).
class Player : public Entity {
 public:
  explicit Player(const math::Vector2d& position) : Entity(position) {}
};

// A box is a specific type of Entity that can be displaced (pulled/pushed)
// around.
class Box : public Entity {
 public:
  explicit Box(const math::Vector2d& position) : Entity(position) {}

  // Store a move applied to the box.
  void AddMove() { ++moves_count_; }

 private:
  // The number of moves this box has been subject to.
  int moves_count_;
};

// Possible tile types for the room.
enum class TileType { kFloor, kWall, kTarget };

// Entity layers.
enum class EntityLayer : int { kPlayer = 0, kBox = 1 };

// This class represents a Pushbox room as a matrix of tiles. Each tile can be
// a wall, floor (where the player and boxes can be placed) or a target (a floor
// corresponding to the final location of a box).
// The room contains a player and an arbitrary number of boxes.
// The state of the room can be modified by applying actions, which correspond
// with a movement direction for the player and an indication of whether the
// player must pull a box (instead of pushing a box, the player will pull boxes
// as this is used for generating Pushbox levels by reversing the player actions
// from a final position).
// This class provides mechanisms for querying the type of tile on a given,
// position, its contents, checking if a specific action can be applied and
// applying the action.
class Room {
 public:
  // The room does not take ownership of the topology vector, so caller should
  // ensure that is lives longer than the room.
  Room(int width, int height, absl::Span<const TileType> topology,
       absl::Span<const std::uint64_t> zobrist_bitstrings);

  // Returns a string representing the room.
  std::string ToString() const;

  // Returns a zobrist hash representing the room state.
  std::uint64_t hash() const { return zobrist_hash_; }

  // Get/set the tile type of a given room position.
  TileType GetTileType(const math::Vector2d& position) const;

  // Methods for querying the type of the tile on a given position.
  bool IsWall(const math::Vector2d& position) const;
  bool IsFloor(const math::Vector2d& position) const;

  bool IsTarget(const math::Vector2d& position) const;
  bool IsEmpty(const math::Vector2d& position) const;

  // Methods for checking the contents of a tile in a given position.
  bool ContainsPlayer(const math::Vector2d& position) const;
  bool ContainsBox(const math::Vector2d& position) const;

  // Set the player position.
  void SetPlayerPosition(const math::Vector2d& position);

  // Get the player position.
  math::Vector2d GetPlayerPosition() const { return player_.position(); }

  // Add an additional box in the given position.
  void AddBox(const math::Vector2d& position);

  // Applies an action to the player and to an adjacent box as well if the
  // action involves pulling.
  // Note the CanApplyAction must be called beforehand to ensure that the action
  // can be applied.
  void ApplyAction(const Action& action);

  // Calculates a score for this room.
  float ComputeScore();

  // Get the score for the room.
  float room_score() const { return room_score_; }

  // Get the number of actions applied to this room.
  int num_actions() const { return num_actions_; }

  // Returns true if the player is on top of a target.
  bool PlayerOnTarget() { return IsTarget(player_.position()); }

  // Returns whether there is any box on top of any target.
  bool BoxOnTarget();

  // Returns the collection of all boxes in the room.
  const std::vector<Box>& GetBoxes() const { return boxes_; }

  // Returns room width.
  int width() const { return width_; }

  // Returns room height.
  int height() const { return height_; }

 private:
  // Applies the given action to the player (i.e. changes the player position).
  // Note the CanApplyAction must be called beforehand to ensure that the action
  // can be applied.
  void ApplyPlayerAction(const math::Vector2d& origin, const Action& action);

  // Moves the box in the origin location in the specified direction.
  // Note the CanApplyAction must be called beforehand to ensure that the move
  // can be applied.
  void MoveBox(const math::Vector2d& origin, const math::Vector2d& direction);

  // Updates zobrist hash.
  void ZobristAddOrRemovePiece(const math::Vector2d& position,
                               EntityLayer layer);

  // Room dimensions.
  int width_, height_, cell_count_;

  // The tiles composing the room (walls, targets, empty spaces, ...). Contains
  // width_*height_ tiles.
  absl::Span<const TileType> topology_;

  // Zobrist bitstrings https://en.wikipedia.org/wiki/Zobrist_hashing
  absl::Span<const std::uint64_t> zobrist_bitstrings_;

  // Zobrist hash https://en.wikipedia.org/wiki/Zobrist_hashing
  std::uint64_t zobrist_hash_;

  // Boxes in the room.
  std::vector<Box> boxes_;

  // The player in the room.
  Player player_;

  // Number of times the ApplyAction method has been invoked.
  int num_actions_;

  // The index of the last moved box.
  int last_box_index_;

  // Number of times the box being moved has changed.
  int moved_box_changes_;

  // Score calculated for the room.
  float room_score_;
};

}  // namespace deepmind::lab2d::pushbox

#endif  // DMLAB2D_LIB_SYSTEM_GENERATORS_PUSHBOX_ROOM_H_