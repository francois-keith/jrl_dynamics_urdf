// Copyright 2011, 2012, Florent Lamiraux, Guido Manfredi, Thomas
// Moulard, JRL, CNRS/AIST.
//
// This file is part of jrl_dynamics_bridge.
// sot-motion-planner is free software: you can redistribute it and/or
// modify it under the terms of the GNU Lesser General Public License
// as published by the Free Software Foundation, either version 3 of
// the License, or (at your option) any later version.
//
// sot-motion-planner is distributed in the hope that it will be useful, but
// WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// General Lesser Public License for more details.  You should have
// received a copy of the GNU Lesser General Public License along with
// sot-motion-planner. If not, see <http://www.gnu.org/licenses/>.

#include <cassert>
#include <stdexcept>
#include <string>
#include <vector>

#include <boost/foreach.hpp>
#include <boost/format.hpp>

#include <LinearMath/btMatrix3x3.h>
#include <LinearMath/btQuaternion.h>

#include "jrl/dynamics/urdf/parser.hh"

namespace jrl
{
  namespace dynamics
  {
    namespace urdf
    {

      CjrlJoint*
      makeJointRotation (Parser::MapJrlJoint& jointsMap,
			 const matrix4d& position,
			 const std::string& name,
			 const double& lower,
			 const double& upper,
			 dynamicsJRLJapan::ObjectFactory& factory)
      {
	CjrlJoint* joint = factory.createJointRotation (position);
	joint->setName (name);
	joint->lowerBound (0, lower);
	joint->upperBound (0, upper);
	jointsMap[name] = joint;
	return joint;
      }

      CjrlJoint*
      makeJointContinuous (Parser::MapJrlJoint& jointsMap,
			   const matrix4d& position,
			   const std::string& name,
			   dynamicsJRLJapan::ObjectFactory& factory)
      {
	return makeJointRotation
	  (jointsMap, position, name, -3.14, 3.14, factory);
      }

      CjrlJoint*
      makeJointTranslation (Parser::MapJrlJoint& jointsMap,
			    const matrix4d& position,
			    const std::string& name,
			    const double& lower,
			    const double& upper,
			    dynamicsJRLJapan::ObjectFactory& factory)
      {
	CjrlJoint* joint = factory.createJointTranslation (position);
	joint->setName (name);
	joint->lowerBound (0, lower);
	joint->upperBound (0, upper);
	jointsMap[name] = joint;
	return joint;
      }

      CjrlJoint*
      makeJointFreeFlyer (Parser::MapJrlJoint& jointsMap,
			  const matrix4d& position,
			  const std::string& name,
			  dynamicsJRLJapan::ObjectFactory& factory)
      {
	CjrlJoint* joint = factory.createJointFreeflyer (position);
	joint->setName (name);
	jointsMap[name] = joint;
	return joint;
      }

      CjrlJoint*
      makeJointAnchor (Parser::MapJrlJoint& jointsMap,
		       const matrix4d& position,
		       const std::string& name,
		       dynamicsJRLJapan::ObjectFactory& factory)
      {
	CjrlJoint* joint = factory.createJointAnchor (position);
	joint->setName (name);
	jointsMap[name] = joint;
	return joint;
      }


      Parser::Parser ()
	: model_ (),
	  robot_ (),
	  rootJoint_ (),
	  jointsMap_ (),
	  factory_ ()
      {}

      Parser::~Parser ()
      {}

      CjrlHumanoidDynamicRobot*
      Parser::parse (const std::string& filename,
		     const std::string& rootJointName)
      {
	// Reset the attributes to avoid problems when loading
	// multiple robots using the same object.
	model_.clear ();
	robot_ = factory_.createHumanoidDynamicRobot ();
	rootJoint_ = 0;
	jointsMap_.clear ();

	// Parse urdf model.
	if (!model_.initFile (filename))
	  throw std::runtime_error ("failed to open URDF file."
				    " Is the filename location correct?");

	// Look for actuated joints into the urdf model tree.
	parseActuatedJoints (rootJointName);
	if (!rootJoint_)
	  throw std::runtime_error ("failed to parse actuated joints");

	// Set model actuated joints.
	std::vector<CjrlJoint*> actJointsVect = actuatedJoints ();
	robot_->setActuatedJoints (actJointsVect);

	// Create the kinematic tree.
	// We iterate over the URDF root joints to connect them to the
	// root link that we added "manually" before. Then we iterate
	// in the whole tree using the connectJoints method.
	boost::shared_ptr<const ::urdf::Link> rootLink = model_.getRoot ();
	if (!rootLink)
	  throw std::runtime_error ("URDF model is missing a root link");

	typedef boost::shared_ptr<const ::urdf::Joint> JointPtr_t;
	BOOST_FOREACH (const JointPtr_t& joint, rootLink->child_joints)
	  {
	    if (!joint)
	      throw std::runtime_error ("null shared pointer in URDF model");
	    MapJrlJoint::const_iterator child = jointsMap_.find (joint->name);
	    if (child == jointsMap_.end () || !child->second)
	      throw std::runtime_error ("missing node in kinematics tree");
	    rootJoint_->addChildJoint (*child->second);
	    connectJoints(child->second);
	  }

	// Notifying special joints
	robot_->waist(jointsMap_["base_footprint_joint"]);
	robot_->chest(jointsMap_["torso_lift_joint"]);
	robot_->leftWrist(jointsMap_["l_gripper_joint"]);
	robot_->rightWrist(jointsMap_["r_gripper_joint"]);

	// Add corresponding body(link) to each joint
	addBodiesToJoints();

	//robot_->initialize();
	return robot_;
      }

      void
      Parser::parseActuatedJoints (const std::string rootJointName)
      {
	// Create free floating joint.
	// FIXME: position set to identity for now.
	matrix4d position;
	position.setIdentity ();
	rootJoint_ = makeJointFreeFlyer (jointsMap_, position, rootJointName,
					 factory_);
	if (!rootJoint_)
	  throw std::runtime_error
	    ("failed to create root joint (free floating)");
	robot_->rootJoint(*rootJoint_);

	// Iterate through each "true cinematic" joint and create a
	// corresponding CjrlJoint.
	for(MapJointType::const_iterator it = model_.joints_.begin();
	    it != model_.joints_.end(); ++it)
	  {
	    // FIXME: compute joint position
	    // position =
	    // getPoseInReferenceFrame("base_footprint_joint",
	    // it->first);

	    switch(it->second->type)
	      {
	      case ::urdf::Joint::UNKNOWN:
		throw std::runtime_error
		  ("parsed joint has UNKNOWN type, this should not happen");
		break;
	      case ::urdf::Joint::REVOLUTE:
		makeJointRotation (jointsMap_, position, it->first,
				   it->second->limits->lower,
				   it->second->limits->upper,
				   factory_);
		break;
	      case ::urdf::Joint::CONTINUOUS:
		makeJointContinuous (jointsMap_, position, it->first, factory_);
		break;
	      case ::urdf::Joint::PRISMATIC:
		makeJointTranslation (jointsMap_, position, it->first,
				      it->second->limits->lower,
				      it->second->limits->upper,
				      factory_);
		break;
	      case ::urdf::Joint::FLOATING:
		makeJointFreeFlyer (jointsMap_, position, it->first, factory_);
		break;
	      case ::urdf::Joint::PLANAR:
		throw std::runtime_error ("PLANAR joints are not supported");
		break;
	      case ::urdf::Joint::FIXED:
		makeJointAnchor (jointsMap_, position, it->first, factory_);
		break;
	      default:
		boost::format fmt
		  ("unknown joint type %1%: should never happen");
		fmt % (int)it->second->type;
		throw std::runtime_error (fmt.str ());
	      }
	  }
      }

      std::vector<CjrlJoint*> Parser::actuatedJoints ()
      {
	std::vector<CjrlJoint*> jointsVect;

	typedef std::map<std::string, boost::shared_ptr< ::urdf::Joint > >
	  jointMap_t;

        for(jointMap_t::const_iterator it = model_.joints_.begin ();
	    it != model_.joints_.end (); ++it)
	  {
	    if (!it->second)
	      throw std::runtime_error ("null joint shared pointer");
	    if (it->second->type == ::urdf::Joint::UNKNOWN
		|| it->second->type == ::urdf::Joint::FLOATING
		|| it->second->type == ::urdf::Joint::FIXED)
	      continue;
	    MapJrlJoint::const_iterator child = jointsMap_.find (it->first);
	    if (child == jointsMap_.end () || !child->second)
	      throw std::runtime_error ("failed to compute actuated joints");
	    jointsVect.push_back (child->second);
	  }
	return jointsVect;
      }

      void
      Parser::connectJoints (CjrlJoint* rootJoint)
      {
	BOOST_FOREACH (const std::string& childName,
		       getChildrenJoint (rootJoint->getName ()))
	  {
	    MapJrlJoint::const_iterator child = jointsMap_.find (childName);
	    if (child == jointsMap_.end () && !!child->second)
	      throw std::runtime_error ("failed to connect joints");
	    rootJoint->addChildJoint(*child->second);
	    connectJoints (child->second);
	  }
      }

      void
      Parser::addBodiesToJoints ()
      {
        for(MapJrlJoint::const_iterator it = jointsMap_.begin();
	    it != jointsMap_.end(); ++it)
	  {
	    // Retrieve associated URDF joint.
	    UrdfJointConstPtrType joint = model_.getJoint (it->first);
	    if (!joint)
	      continue;

	    // Retrieve joint name.
	    std::string childLinkName = joint->child_link_name;

	    // Get child link.
	    UrdfLinkConstPtrType link = model_.getLink (childLinkName);
	    if (!link)
	      throw std::runtime_error ("inconsistent model");

	    // Retrieve inertial information.
	    boost::shared_ptr< ::urdf::Inertial> inertial =
	      link->inertial;

	    vector3d localCom (0., 0., 0.);
	    matrix3d inertiaMatrix;
	    inertiaMatrix.setIdentity();
	    double mass = 0.;
	    if (inertial)
	      {
		//FIXME: properly re-orient the frames.
		localCom[0] = inertial->origin.position.x;
		localCom[1] = inertial->origin.position.y;
		localCom[2] = inertial->origin.position.z;

		mass = inertial->mass;

		inertiaMatrix (0, 0) = inertial->ixx;
		inertiaMatrix (0, 1) = inertial->ixy;
		inertiaMatrix (0, 2) = inertial->ixz;

		inertiaMatrix (1, 0) = inertial->ixy;
		inertiaMatrix (1, 1) = inertial->iyy;
		inertiaMatrix (1, 2) = inertial->iyz;

		inertiaMatrix (2, 0) = inertial->ixz;
		inertiaMatrix (2, 1) = inertial->iyz;
		inertiaMatrix (2, 2) = inertial->izz;
	      }
	    else
	      std::cerr
		<< "WARNING: missing inertial information in model"
		<< std::endl;

	    // TODO get center of mass in local frame, inertia matrix
	    // in global frame and body mass

	    // Create body and fill its fiels..
	    BodyPtrType body = factory_.createBody();
	    body->mass(mass);
	    body->localCenterOfMass(localCom);
	    body->inertiaMatrix(inertiaMatrix);
	    // Link body to joint.
	    it->second->setLinkedBody(*body);
	  }
      }

      std::vector<std::string>
      Parser::getChildrenJoint (const std::string& jointName)
      {
	std::vector<std::string> result;
	getChildrenJoint (jointName, result);
	return result;
      }

      void
      Parser::getChildrenJoint (const std::string& jointName,
				std::vector<std::string>& result)
      {
	typedef boost::shared_ptr< ::urdf::Joint> jointPtr_t;

	boost::shared_ptr<const ::urdf::Joint> joint =
	  model_.getJoint(jointName);

	if (!joint)
	  {
	    boost::format fmt
	      ("failed to retrieve children joints of joint %s");
	    fmt % jointName;
	    throw std::runtime_error (fmt.str ());
	  }

	boost::shared_ptr<const ::urdf::Link> childLink =
	  model_.getLink (joint->child_link_name);

	if (!childLink)
	  throw std::runtime_error ("failed to retrieve children link");

       	const std::vector<jointPtr_t>& jointChildren =
	  childLink->child_joints;

	BOOST_FOREACH (const jointPtr_t& joint, jointChildren)
	  {
	    if (jointsMap_.count(joint->name) > 0)
	      result.push_back (joint->name);
	    else
	      getChildrenJoint (joint->name, result);
	  }
      }

      matrix4d
      Parser::getPoseInReferenceFrame(const std::string& referenceJointName,
				      const std::string& currentJointName)
      {
	if(referenceJointName.compare(currentJointName) == 0)
	  return poseToMatrix
	    (model_.getJoint
	     (currentJointName)->parent_to_joint_origin_transform);

	// get transform to parent link
	::urdf::Pose jointToParentTransform =
	    model_.getJoint(currentJointName)->parent_to_joint_origin_transform;
	matrix4d transform = poseToMatrix(jointToParentTransform);
	// move to next parent joint
	std::string parentLinkName =
	  model_.getJoint(currentJointName)->parent_link_name;
	std::string parentJointName =
	  model_.getLink(parentLinkName)->parent_joint->name;
	transform *= getPoseInReferenceFrame(referenceJointName,
					     parentJointName);

	return transform;
      }

      matrix4d Parser::poseToMatrix(::urdf::Pose p)
      {
	matrix4d t;

	// Fill rotation part.
	btQuaternion q (p.rotation.x, p.rotation.y,
			p.rotation.z, p.rotation.w);
	btMatrix3x3 rotationMatrix (q);
	for (unsigned i = 0; i < 3; ++i)
	  for (unsigned j = 0; j < 3; ++j)
	    t (i, j) = rotationMatrix[i][j];

	// Fill translation part.
	t (0, 3) = p.position.x;
	t (1, 3) = p.position.y;
	t (2, 3) = p.position.z;
	t (3, 3) = 1.;

	t(3, 0) = t(3, 1) = t(3, 2) = 0.;

	return t;
      }
    } // end of namespace urdf.
  } // end of namespace dynamics.
} // end of namespace  jrl.
