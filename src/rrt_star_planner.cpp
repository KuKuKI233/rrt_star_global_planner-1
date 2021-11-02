/*
  Rafael Barreto, 2021
  rrt_star_planner.cpp

*/

#include <ros/ros.h>
#include <pluginlib/class_list_macros.h>


#include "rrt_star_global_planner/rrt_star_planner.hpp"


// TODO set size of vector nodes_(max_num_nodes_)


//register this planner as a BaseGlobalPlanner plugin
PLUGINLIB_EXPORT_CLASS(rrt_star_global_planner::RRTStarPlanner, nav_core::BaseGlobalPlanner)

namespace rrt_star_global_planner {


RRTStarPlanner::RRTStarPlanner() 
        : costmap_(NULL), initialized_(false) {}

RRTStarPlanner::RRTStarPlanner(std::string name, costmap_2d::Costmap2DROS* costmap_ros) 
      : costmap_ros_(costmap_ros) {
    //initialize the planner
    initialize(name, costmap_ros);
}

void RRTStarPlanner::initialize(std::string name, costmap_2d::Costmap2DROS* costmap_ros) {
  if (!initialized_) {
    // Initialize map
    costmap_ros_ = costmap_ros;
    costmap_ = costmap_ros->getCostmap();

    ros::NodeHandle private_nh("~/" + name);

    // TODO
    // hard coding for while
    origin_x_ = costmap_->getOriginX();
    origin_y_ = costmap_->getOriginY();
    resolution_ = costmap_->getResolution();
    goal_tolerance_ = 0.5;
    epsilon_ = 0.1;
    map_width_ = 10.0;
    map_height_ = 10.0;
    radius_ = 1.0;
    min_number_nodes_ = 1000;
    max_number_nodes_ = 10000;

    // Random
    random_double_.setRange(-map_width_, map_width_);

    ROS_INFO("RRT* planner initialized successfully");
    initialized_ = true;
  } else {
    ROS_WARN("This planner has already been initialized... doing nothing");
  }

  ROS_INFO("origin_x_: %.2lf", origin_x_);
  ROS_INFO("origin_y_: %.2lf", origin_y_);
  ROS_INFO("resolution_: %.2lf", resolution_);
  ROS_INFO("goal_tolerance_: %.2lf", goal_tolerance_);
  ROS_INFO("epsilon_: %.2lf", epsilon_);
  ROS_INFO("map_width_: %.2lf", map_width_);
  ROS_INFO("map_height_: %.2lf", map_height_);
  ROS_INFO("radius_: %.2lf", radius_);
  ROS_INFO("min_number_nodes_: %d", min_number_nodes_);
  ROS_INFO("max_number_nodes_: %d", max_number_nodes_);
  ROS_INFO("test random double 1: %.2lf", random_double_.generate());
  ROS_INFO("test random double 2: %.2lf", random_double_.generate());
  ROS_INFO("test random double 3: %.2lf", random_double_.generate());
  
}

bool RRTStarPlanner::makePlan(const geometry_msgs::PoseStamped& start,
                              const geometry_msgs::PoseStamped& goal,
                              std::vector<geometry_msgs::PoseStamped>& plan) {
  //clear the plan, just in case
  plan.clear();

  // Initialize the goal node
  goal_node_.x = goal.pose.position.x;
  goal_node_.y = goal.pose.position.y;

  ROS_INFO("RRT* Global Planner");
  ROS_INFO("Current Position: ( %.2lf, %.2lf)", start.pose.position.x, start.pose.position.y);
  ROS_INFO("GOAL Position: ( %.2lf, %.2lf)", goal.pose.position.x, goal.pose.position.y);

  std::list<std::pair<float, float>> path;

  // TODO remove this
  std::string global_frame = frame_id_;

  Node start_node;
  start_node.x = start.pose.position.x;
  start_node.y = start.pose.position.y;
  start_node.node_id = 0;
  start_node.parent_id = -1; // None parent node
  start_node.cost = 0.0;

  // Add the start node
  nodes_.push_back(start_node);
  
  std::pair<float, float> p_rand;
  std::pair<float, float> p_new;

  Node node_nearest;

  while (nodes_.size() < max_number_nodes_) {
    bool found_next = false;
    while (found_next == false) {
      p_rand = sampleFree(); // random point in the free space
      node_nearest = getNearest(p_rand); // The nearest node of the random point
      p_new = steer(node_nearest.x, node_nearest.y, p_rand.first, p_rand.second); // new point and node candidate.
      if (obstacleFree(node_nearest, p_new.first, p_new.second)) {
        found_next = true;

        // TODO new method encapsulare Node creation?
        Node new_node(p_new.first, p_new.second, (int)nodes_.size(), node_nearest.node_id);

        // Optimize
        chooseParent(node_nearest, new_node); // Select the best parent
        nodes_.push_back(new_node);
        rewire(new_node); 
      }
    }
    // Check if the distance between the goal and the new node is less than
    // the goal tolerance
    if (isGoalReached(p_new) && nodes_.size() > min_number_nodes_) {
      ROS_INFO("RRT* Global Planner: Path found!!!!");
      
      // New goal inside of the goal tolerance
      goal_node_ = nodes_[nodes_.size() - 1];

      Node current_node = goal_node_;

      // Final Path
      std::pair<float, float> point;
      while(current_node.parent_id != -1) {
        point.first = current_node.x;
        point.second = current_node.y;
        path.push_front(point);

        // update the current node
        current_node = nodes_[current_node.parent_id];
      }
  
      //if the global planner find a path
      if(path.size() > 0) {

        // Add the initial Pose
        plan.push_back(start);

        ros::Time plan_time = ros::Time::now();
        // convert the points to poses
        for(auto p : path) {
          geometry_msgs::PoseStamped pose;
          pose.header.stamp = plan_time;
          pose.header.frame_id = "map";
          pose.pose.position.x = p.first;
          pose.pose.position.y = p.second;
          pose.pose.position.z = 0.0;
          pose.pose.orientation.x = 0.0;
          pose.pose.orientation.y = 0.0;
          pose.pose.orientation.z = 0.0;
          pose.pose.orientation.w = 1.0;
          plan.push_back(pose);
        }
        return true;
      }
    }
  }
  ROS_WARN("The planner failed to find a path, choose other goal position");
  return false;
}

std::pair<float, float> RRTStarPlanner::sampleFree() {
  std::pair<float, float> random_point;
  for (int i = 0; i < 10000; i++) {
    // generate random x and y coords within map bounds

    random_point.first = random_double_.generate();
    random_point.second = random_double_.generate();

    if (!collision(random_point.first, random_point.second))
      return random_point;
  }
  // TODO
  // ROS_ERROR() not point found

  return random_point;
}

bool RRTStarPlanner::collision(float wx, float wy) {
  // TODO check this method
  int mx, my;
  worldToMap(wx, wy, mx, my);

  if ((mx < 0) || (my < 0) || (mx >= costmap_->getSizeInCellsX()) || (my >= costmap_->getSizeInCellsY()))
    return true;

  // TODO static_cast? check this 
  unsigned int cost = static_cast<int>(costmap_->getCost(mx, my));
  if (cost > 0)
    return true;
  
  return false;
}

Node RRTStarPlanner::getNearest(const std::pair<float, float> &p_rand) {
  Node node_nearest = nodes_[0];

  float dist_nearest, dist;
  for (int i = 1; i < nodes_.size(); ++i) {
    dist_nearest = euclideanDistance2D(node_nearest.x, node_nearest.y, p_rand.first, p_rand.second);
    dist = euclideanDistance2D(nodes_[i].x, nodes_[i].y, p_rand.first, p_rand.second);
    if (dist < dist_nearest) node_nearest = nodes_[i];
  }

  return node_nearest;
}

void RRTStarPlanner::chooseParent(Node &parent_node, Node &new_node) {
  float cost_new_node;
  float cost_other_parent;
  float nodes_dist;
  Node node;

  // TODO use iterator
  for (int i = 0; i < nodes_.size(); ++i) {
    node = nodes_[i];
    // distance between node and new_node
    nodes_dist = euclideanDistance2D(node.x, node.y, new_node.x, new_node.y);

    if (nodes_dist < radius_) {
      // current cost of new_node
      cost_new_node = parent_node.cost + euclideanDistance2D(parent_node.x, parent_node.y, new_node.x, new_node.y);
      
      // cost if the parent is node
      cost_other_parent = node.cost + nodes_dist;

      if (cost_other_parent < cost_new_node) {
        if (obstacleFree(node, parent_node.x, parent_node.y)) {
          parent_node = node;
        }
      }
    }
  }

  // Update new_node cost and its new parent
  new_node.cost = parent_node.cost + euclideanDistance2D(parent_node.x, parent_node.y, new_node.x, new_node.y);
  new_node.parent_id = parent_node.node_id;
}


// TODO clean the if statement
void RRTStarPlanner::rewire(Node new_node) {
  Node node;
  for (int i = 0; i < nodes_.size(); i++) {
    node = nodes_[i];
    if (node != nodes_[new_node.parent_id] &&
        euclideanDistance2D(node.x, node.y, new_node.x, new_node.y) < radius_ &&
        new_node.cost + euclideanDistance2D(node.x, node.y, new_node.x, new_node.y) < node.cost &&
        obstacleFree(node, new_node.x, new_node.y)) {
      node.parent_id = new_node.node_id;
      node.cost = new_node.cost + euclideanDistance2D(node.x, node.y, new_node.x, new_node.y);
    }
  }
}

// TODO change parameters name
std::pair<float, float> RRTStarPlanner::steer(float x1, float y1, float x2, float y2) {
  std::pair<float, float> p_new;
  float dist = euclideanDistance2D(x1, y1, x2, y2);
  if (dist < epsilon_) {
    p_new.first = x1;
    p_new.second = y1;
    return p_new;
  } else {
    float theta = atan2(y2-y1, x2-x1);
    p_new.first = x1 + epsilon_*cos(theta);
    p_new.second = y1 + epsilon_*sin(theta);
    return p_new;
  }
}

bool RRTStarPlanner::obstacleFree(const Node &node_nearest, float px, float py) {
  int n = 1;
  float theta;

  std::pair<float, float> p_n;
  p_n.first = 0.0;
  p_n.second = 0.0;

  float dist = euclideanDistance2D(node_nearest.x, node_nearest.y, px, py);
  if (dist < resolution_) {
    if (collision(px, py))
      return false;
    else
      return true;
  } else {
    int value = int(floor(dist/resolution_));
    float theta;
    for (int i = 0; i < value; i++) {
      theta = atan2(node_nearest.y - py, node_nearest.x - px);
      p_n.first = node_nearest.x + n*resolution_*cos(theta);
      p_n.second = node_nearest.y + n*resolution_*sin(theta);
      if (collision(p_n.first, p_n.second))
        return false;
      
      n++;
    }
    return true;
  }
}

void RRTStarPlanner::worldToMap(float wx, float wy, int& mx, int& my) {
  mx = (wx - origin_x_) / resolution_;
  my = (wy - origin_y_) / resolution_;
}

bool RRTStarPlanner::isGoalReached(const std::pair<float, float> &p_new) {
  return (euclideanDistance2D(p_new.first,
                              p_new.second,
                              goal_node_.x,
                              goal_node_.y) < goal_tolerance_) ? true : false;
}
} // RRTstar_planner namespace

