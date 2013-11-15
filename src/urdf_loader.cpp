/** \file urdf_loader.cpp
 * \brief Implementation of a URDF loading plugin for OpenRAVE
 * \author Pras Velagapudi
 * \date 2013
 */

/* (C) Copyright 2013 Carnegie Mellon University */

#include "urdf_loader.h"
#include "urdf_yaml_helpers.h"
#include "boostfs_helpers.h"

#include <tinyxml.h>
#include <urdf/model.h>
#include <boost/foreach.hpp>
#include <boost/make_shared.hpp>
#include <boost/format.hpp>
#include <boost/filesystem.hpp>
#include <ros/package.h>
#include <yaml-cpp/yaml.h>

/** Boilerplate plugin definition for OpenRAVE */
OpenRAVE::InterfaceBasePtr CreateInterfaceValidated(OpenRAVE::InterfaceType type, const std::string& interfacename, std::istream& sinput, OpenRAVE::EnvironmentBasePtr env)
{
  if( type == OpenRAVE::PT_Module && interfacename == "urdf" ) {
    return OpenRAVE::InterfaceBasePtr(new or_urdf::URDFLoader(env));
  } else {
    return OpenRAVE::InterfaceBasePtr();
  }
}

/** Boilerplate plugin definition for OpenRAVE */
void GetPluginAttributesValidated(OpenRAVE::PLUGININFO& info)
{
  info.interfacenames[OpenRAVE::PT_Module].push_back("URDF");
}

/** Boilerplate plugin definition for OpenRAVE */
void DestroyPlugin()
{
  // Nothing to clean up!
}

namespace or_urdf
{
  /** Converts from URDF 3D vector to OpenRAVE 3D vector. */
  OpenRAVE::Vector URDFVectorToRaveVector(const urdf::Vector3 &vector)
  {
    return OpenRAVE::Vector(vector.x, vector.y, vector.z);
  }

  /** Converts from URDF 3D rotation to OpenRAVE 3D vector. */
  OpenRAVE::Vector URDFRotationToRaveVector(const urdf::Rotation &rotation)
  {
    return OpenRAVE::Vector(rotation.x, rotation.y, rotation.z, rotation.w);
  }

  OpenRAVE::Vector URDFColorToRaveVector(const urdf::Color &color)
  {
    return OpenRAVE::Vector(color.r, color.g, color.b, color.a);
  }

  OpenRAVE::Transform URDFPoseToRaveTransform(const urdf::Pose &pose)
  {
    return OpenRAVE::Transform(URDFRotationToRaveVector(pose.rotation),
                               URDFVectorToRaveVector(pose.position));
  }
  
  /** Resolves URIs for file:// and package:// paths */
  const std::string resolveURI(const std::string &path)
  {
    static std::map<std::string, std::string> package_cache;
    std::string uri = path;

    if (uri.find("file://") == 0) {

      // Strip off the file://
      uri.erase(0, strlen("file://"));

      // Resolve the mesh path as a file URI
      boost::filesystem::path file_path(uri);
      return file_path.string();

    } else if (uri.find("package://") == 0) {

      // Strip off the package://
      uri.erase(0, strlen("package://"));
	
      // Resolve the mesh path as a ROS package URI
      size_t package_end = uri.find("/");
      std::string package = uri.substr(0, package_end);
      std::string package_path;

      // Use the package cache if we have resolved this package before
      std::map<std::string, std::string>::iterator it = package_cache.find(package);
      if (it != package_cache.end()) {
	package_path = it->second;
      } else {
	package_path = ros::package::getPath(package);
	package_cache[package] = package_path;
      }
      
      // Show a warning if the package was not resolved
      if (package_path.empty())	{
	RAVELOG_WARN("Unable to find package [%s].\n", package.c_str());
	return "";
      }
      
      // Append the remaining relative path
      boost::filesystem::path file_path(package_path);
      uri.erase(0, package_end);
      file_path /= uri;
      
      // Return the canonical path
      return file_path.string();

    } else {
      RAVELOG_WARN("Cannot handle mesh URI type [%s].\n");
      return "";
    }
  }

  /** Converts URDF joint to an OpenRAVE joint string and a boolean
      representing whether the joint is moving or fixed */
  const std::pair<OpenRAVE::KinBody::JointType, bool> URDFJointTypeToRaveJointType(int type)
  {
    switch(type) {
    case urdf::Joint::REVOLUTE:
      return std::make_pair(OpenRAVE::KinBody::JointRevolute, true);
    case urdf::Joint::PRISMATIC:
      return std::make_pair(OpenRAVE::KinBody::JointSlider, true);
    case urdf::Joint::FIXED:
      return std::make_pair(OpenRAVE::KinBody::JointHinge, false);
    case urdf::Joint::CONTINUOUS:
      return std::make_pair(OpenRAVE::KinBody::JointHinge, true);
    case urdf::Joint::PLANAR:
    case urdf::Joint::FLOATING:
    case urdf::Joint::UNKNOWN:
    default:
      // TODO: Fill the rest of these joint types in!
      RAVELOG_ERROR("URDFLoader : Unable to determine joint type [%d].\n", type);
      throw OpenRAVE::openrave_exception("Failed to convert URDF joint!");
    }
  }
  
  void makeTextElement(TiXmlElement *element, const std::string &name, const std::string &value)
  {
    TiXmlElement *node = new TiXmlElement(name);  
    node->LinkEndChild(new TiXmlText(value));  
    element->LinkEndChild(node);
  }
  
  class Geometry {
  public:
    enum Type { COLLISION, RENDER };
  };

  /** Static empty mesh (used as placeholder when no geometry exists) */
  static const std::string empty_filename = resolveURI("package://or_urdf/empty.iv");

  TiXmlElement *makeGeomElement(const urdf::Geometry &geometry, Geometry::Type type)
  {
    TiXmlElement *node = new TiXmlElement("Geom");
    // TODO: set a "render" attribute depending on collision or render

    // Convert depending on geometry type
    switch(geometry.type) {
    case urdf::Geometry::SPHERE:
      {
	node->SetAttribute("type", "sphere");

	const urdf::Sphere &sphere = dynamic_cast<const urdf::Sphere&>(geometry);
	makeTextElement(node, "radius", boost::lexical_cast<std::string>(sphere.radius));
      }
      break;
    case urdf::Geometry::BOX:
      {
	node->SetAttribute("type", "box");

	const urdf::Box &box = dynamic_cast<const urdf::Box&>(geometry);
	makeTextElement(node, "extents", boost::str(boost::format("%f %f %f")
						    % box.dim.x % box.dim.y % box.dim.z ));
      }
      break;
    case urdf::Geometry::CYLINDER:
      {
	node->SetAttribute("type", "cylinder");

	const urdf::Cylinder &cylinder = dynamic_cast<const urdf::Cylinder&>(geometry);
	makeTextElement(node, "height", boost::lexical_cast<std::string>(cylinder.length));
	makeTextElement(node, "radius", boost::lexical_cast<std::string>(cylinder.radius));
      }
      break;
    case urdf::Geometry::MESH:
      {
	const urdf::Mesh &mesh = dynamic_cast<const urdf::Mesh&>(geometry);
	std::string mesh_filename = resolveURI(mesh.filename);

	// Either create a collision or render geometry
	switch(type) {
	case Geometry::COLLISION:
          node->SetAttribute("type", "trimesh");
          node->SetAttribute("render", "false");
       	  makeTextElement(node, "Data", mesh_filename);
	  break;
	case Geometry::RENDER:
          node->SetAttribute("type", "sphere");
          makeTextElement(node, "radius", "0.0");
	  makeTextElement(node, "Render", mesh_filename);
	  break;
	default:
	  RAVELOG_ERROR("URDFLoader : Unable to determine trimesh type [%d].\n", type);
	  throw OpenRAVE::openrave_exception("Failed to convert URDF trimesh!");
	}
      }
      break;
    default:
      RAVELOG_ERROR("URDFLoader : Unable to determine geometry type [%d].\n", geometry.type);
      throw OpenRAVE::openrave_exception("Failed to convert URDF geometry!");
    }
    
    return node;
  }

  /** Opens a URDF file and returns a robot in OpenRAVE */
  bool URDFLoader::load(std::ostream &soutput, std::istream &sinput)
  {
    // Get filename from input arguments
    std::string urdf_filename;
    sinput >> urdf_filename;

    // Get the config file from input arguments
    std::string config_filename;
    sinput >> config_filename;

    // Parse file via URDF reader
    urdf::Model model;
    if (!model.initFile(urdf_filename)) {
      RAVELOG_ERROR("URDFLoader : Unable to open URDF file [%s].\n", urdf_filename.c_str());
      throw OpenRAVE::openrave_exception("Failed to open URDF file!");
    }

    // Populate list of links from URDF model
    std::vector< boost::shared_ptr<urdf::Link> > link_vector;
    model.getLinks(link_vector);
    std::list< boost::shared_ptr<urdf::Link> > link_list(link_vector.begin(), link_vector.end());
    std::set<std::string> finished_links;

    // TODO: prevent infinite loops here
    // Iterate through all links, allowing deferred evaluation (putting links
    // back on the list) if their parents do not exist yet
    boost::shared_ptr<urdf::Link> link_ptr;

    std::vector<OpenRAVE::KinBody::LinkInfoConstPtr> link_infos;
    while (!link_list.empty()) {
      // Get next element in list
      link_ptr = link_list.front();
      link_list.pop_front();

      OpenRAVE::KinBody::LinkInfoPtr link_info = boost::make_shared<OpenRAVE::KinBody::LinkInfo>();

      link_info->_name = link_ptr->name;
      // TODO: Set "type" to "dynamic".

      // TODO: is this at all reasonable?
      // Set local transformation to be same as parent joint
      boost::shared_ptr<urdf::Joint> parent_joint = link_ptr->parent_joint;
      if (parent_joint) {
        link_info->_t = URDFPoseToRaveTransform(parent_joint->parent_to_joint_origin_transform);
      }
      
      // Set inertial parameters
      boost::shared_ptr<urdf::Inertial> inertial = link_ptr->inertial;
      if (inertial) {
        // XXX: We should also specify the off-diagonal terms (ixy, iyz, ixz)
        // of the inertia tensor. We can do this in KinBody XML files, but I
        // cannot figure out how to do so through this API.
        link_info->_mass = inertial->mass;
        link_info->_tMassFrame = URDFPoseToRaveTransform(inertial->origin);
        link_info->_vinertiamoments = OpenRAVE::Vector(inertial->ixx, inertial->iyy, inertial->izz);
      }

      // Set information for collision geometry
      //link_info->_vgeometryinfos
      boost::shared_ptr<urdf::Collision> collision = link_ptr->collision;
      if (collision) {
        OpenRAVE::KinBody::GeometryInfoPtr geom_info = boost::make_shared<OpenRAVE::KinBody::GeometryInfo>();

        geom_info->_t = URDFPoseToRaveTransform(collision->origin);
        geom_info->_bVisible = false;
        geom_info->_bModifiable = false;

        switch (collision->geometry->type) {
        case urdf::Geometry::MESH: {
            const urdf::Mesh &mesh = dynamic_cast<const urdf::Mesh &>(*collision->geometry);
            geom_info->_filenamecollision = resolveURI(mesh.filename);
            geom_info->_type = OpenRAVE::GT_TriMesh;

            boost::shared_ptr<OpenRAVE::TriMesh> trimesh = boost::make_shared<OpenRAVE::TriMesh>();
            trimesh = GetEnv()->ReadTrimeshURI(trimesh, geom_info->_filenamecollision);
            if (trimesh) {
                geom_info->_meshcollision = *trimesh;
            } else {
                RAVELOG_WARN("Link[%s]: Failed loading collision mesh %s\n",
                             link_ptr->name.c_str(), geom_info->_filenamecollision.c_str());
            }
            break;
        }

        case urdf::Geometry::SPHERE: {
            const urdf::Sphere &sphere = dynamic_cast<const urdf::Sphere &>(*collision->geometry);
            geom_info->_vGeomData = sphere.radius * OpenRAVE::Vector(1, 1, 1);
            geom_info->_type = OpenRAVE::GT_Sphere;
            break;
        }

        case urdf::Geometry::BOX: {
            const urdf::Box &box = dynamic_cast<const urdf::Box &>(*collision->geometry);
            geom_info->_vGeomData = 0.5 * OpenRAVE::Vector(box.dim.x, box.dim.y, box.dim.z);
            geom_info->_type = OpenRAVE::GT_Box;
            break;
        }

        case urdf::Geometry::CYLINDER: {
            const urdf::Cylinder &cylinder = dynamic_cast<const urdf::Cylinder &>(*collision->geometry);
            geom_info->_vGeomData = OpenRAVE::Vector(cylinder.radius, cylinder.length, 0);
            geom_info->_type = OpenRAVE::GT_Cylinder;
            break;
        }
        }
        link_info->_vgeometryinfos.push_back(geom_info);
      }

      // Add the render geometry. We can't create a link with no collision
      // geometry, so we'll instead create a zero-radius sphere with the
      // desired render mesh.
      boost::shared_ptr<urdf::Visual> visual = link_ptr->visual;
      if (visual) {
        OpenRAVE::KinBody::GeometryInfoPtr geom_info = boost::make_shared<OpenRAVE::KinBody::GeometryInfo>();
        geom_info->_t = URDFPoseToRaveTransform(collision->origin);
        geom_info->_type = OpenRAVE::GT_Sphere;
        geom_info->_vGeomData = OpenRAVE::Vector(0.0, 0.0, 0.0);
        geom_info->_bModifiable = false;
        geom_info->_bVisible = true;

        switch (visual->geometry->type) {
        case urdf::Geometry::MESH: {
            const urdf::Mesh &mesh = dynamic_cast<const urdf::Mesh&>(*visual->geometry);
            geom_info->_filenamerender = resolveURI(mesh.filename);
            geom_info->_vRenderScale = OpenRAVE::Vector(1.0, 1.0, 1.0);
            break;
        }

        default:
            RAVELOG_WARN("Link[%s]: Only trimeshes are supported for visual geometry.\n", link_ptr->name.c_str());
        }

        // If a material color is specified, use it.
        boost::shared_ptr<urdf::Material> material = visual->material;
        if (material) {
            geom_info->_vDiffuseColor = URDFColorToRaveVector(material->color);
            geom_info->_vAmbientColor = URDFColorToRaveVector(material->color);
        }
        link_info->_vgeometryinfos.push_back(geom_info);
      }
      
      // Mark this link as completed
      link_infos.push_back(link_info);
    }

    // Populate vector of joints
    std::string joint_name; 
    boost::shared_ptr<urdf::Joint> joint_ptr;

    // Parse the yaml file
    YAML::Node doc;
    std::vector<boost::shared_ptr<urdf::Joint> > ordered_joints;
    
    std::ifstream fin(config_filename.c_str());
    if(fin.is_open()){
        YAML::Parser parser(fin);
        parser.GetNextDocument(doc);
        const YAML::Node *joints = doc.FindValue("joints");
        std::map<std::string, int> jmap;
        if(joints){
            *joints >> jmap;
        }

        // Now create an ordered list of the joints
        ordered_joints.resize(jmap.size());
        BOOST_FOREACH(boost::tie(joint_name, joint_ptr), model.joints_) {
            std::map<std::string, int>::iterator it = jmap.find(joint_name);
            
            // Add the joint to the appropriate point in the list
            if(it != jmap.end()){
                ordered_joints[it->second] = joint_ptr;
            }else{
                ordered_joints.push_back(joint_ptr);
            }
        }
    }else{
        // Just make the ordered joints the model joints
        BOOST_FOREACH(boost::tie(joint_name, joint_ptr), model.joints_) {
            ordered_joints.push_back(joint_ptr);
        }
    }

    std::vector<OpenRAVE::KinBody::JointInfoConstPtr> joint_infos;
    BOOST_FOREACH(boost::shared_ptr<urdf::Joint> joint_ptr, ordered_joints) {
      OpenRAVE::KinBody::JointInfoPtr joint_info = boost::make_shared<OpenRAVE::KinBody::JointInfo>();
      joint_info->_name = joint_ptr->name;
      joint_info->_linkname0 = joint_ptr->parent_link_name;
      joint_info->_linkname1 = joint_ptr->child_link_name;
      joint_info->_vanchor = URDFVectorToRaveVector(joint_ptr->parent_to_joint_origin_transform.position);
      // XXX: What about offsetfrom in the KinBody XML?

      // Set the joint type. Some URDF joints correspond to disabled OpenRAVE
      // joints, so we'll appropriately set the corresponding IsActive flag.
      OpenRAVE::KinBody::JointType joint_type;
      bool enabled;
      boost::tie(joint_type, enabled) = URDFJointTypeToRaveJointType(joint_ptr->type);
      joint_info->_type = joint_type;
      joint_info->_bIsActive = enabled;


      // URDF only supports linear mimic joints with a constant offset. We map
      // that into the correct position (index 0) and velocity (index 1)
      // equations for OpenRAVE.
      // XXX: Mimic joints don't work properly.
#if 0
      boost::shared_ptr<urdf::JointMimic> mimic = joint_ptr->mimic;
      if (mimic) {
        joint_info->_vmimic[0]->_equations[0] = boost::str(boost::format("%s*%0.6f+%0.6f")
                                                  % mimic->joint_name % mimic->multiplier % mimic->offset);
        joint_info->_vmimic[0]->_equations[1] = boost::str(boost::format("|%s %0.6f")
                                                  % mimic->joint_name % mimic->multiplier);
        joint_info->_vmimic[0]->_equations[2] = "0";
      }
#endif

      // Configure joint axis. Add an arbitrary axis if the joint is disabled.
      urdf::Vector3 joint_axis;
      if (enabled) {
        joint_axis = joint_ptr->parent_to_joint_origin_transform.rotation * joint_ptr->axis;
      } else {
        joint_axis = urdf::Vector3(1, 0, 0);
      }
      joint_info->_vaxes[0] = URDFVectorToRaveVector(joint_axis);
      
      // Configure joint limits.
      boost::shared_ptr<urdf::JointLimits> limits = joint_ptr->limits;
      if (limits) {
          joint_info->_vlowerlimit[0] = limits->lower;
          joint_info->_vupperlimit[0] = limits->upper;
          joint_info->_vmaxvel[0] = limits->velocity;
          joint_info->_vmaxtorque[0] = limits->effort;
      } else if (!enabled) {
          joint_info->_vlowerlimit[0] = 0;
          joint_info->_vupperlimit[0] = 0;
      }

      joint_infos.push_back(joint_info);
    }

    // TODO: Output link adjacencies.
#if 0
    if(fin.is_open()){
        YAML::Node const &adjacent_yaml = doc["adjacent"];
        for (size_t i = 0; i < adjacent_yaml.size(); ++i) {
            std::string const link1 = adjacent_yaml[i][0].to<std::string>();
            std::string const link2 = adjacent_yaml[i][1].to<std::string>();
            makeTextElement(kinBody, "Adjacent", link1 + " " + link2);
        }
    }
#endif

    // Create the KinBody.
    // XXX: Allow the user to specify the name.
    OpenRAVE::KinBodyPtr kinbody = OpenRAVE::RaveCreateKinBody(GetEnv(), "");
    kinbody->Init(link_infos, joint_infos);
    kinbody->SetName("urdf");
    GetEnv()->Add(kinbody, true);
    return true;
  }

} 

