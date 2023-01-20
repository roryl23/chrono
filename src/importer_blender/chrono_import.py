# 
# CHRONO IMPORT ADD-ON FOR BLENDER
#
# This is an add-on for Blender3D that allows loading simulations performed with 
# Chrono (http://www.projectchrono.org) and saved with the Chrono POSTPROCESS module.
# 
# To install it: 
# - open Blender
# - menu "Edit/Preferences..", go to tab "Add-ons", press "Install" button and select 
#   this .py file
# - check that the Chorno Import add-on is now available in the list, and it is enabled
#
# To use it: 
# - check that you built the c++ Chrono with the POSTPROCESS module enabled (if you use
#   PyChrono, the postprocess module is always available in the AnaConda installation)
# - write a Chrono app that uses instructions like my_blender_exporter.AddAll(); 
#   my_blender_exporter.ExportScript(); and my_blender_exporter.ExportData(); (the latter
#   in the while() simulation loop). See demo_POST_blender1.cpp for an example.
# - run the chrono app,  this will generate files on disk: a single
#   xxx.assets.py file and many state00001.py, state00002.py, ..., in an output/ dir.
# - Open Blender, use menu "File/Import/Chrono import" to load the xxx.assets.py file.
#
# Tips:
# - When scrubbing the timeline, the animation should happen. This may be a bit slower than 
#   expected because the Chrono add-on loads and unloads from disk the content of each time
#   step, for optimizing memory requirements.
# - use the "Chrono" sidebar at the right border of the 3D view to enable/disable the
#   showing of coordinate systems of Chrono bodies and assets, and other view settings.
# - when possible, the Chrono Blender postprocessor and this add-on tend to optimize memory
#   by sharing identical assets. More in detail, assets for whom myasset->IsMutable() is false
#   in Chrono, ex. spheres, boxes, not deformable meshes, etc., will be put in the "chrono_assets"
#   collection in the Blender outliner interface. Click on the eye icon to show-hide them in the viewport. 
#   This allows you to edit and improve those assets by hand in Blender, it is enough that you 
#   do not change their name. 
# - if no material is added to a visual shape in Chrono, you can add by hand in Blender to 
#   the asset object available in "chrono_assets" collection. (link to data, not to object);
#   if a material is added to a visual shape in Chrono, it overrides the material you add to
#   the asset object available in "chrono_assets" collection (unless you disable "Chrono materials" in
#   the Chrono sidebar panel).



bl_info = {
    "name": "Chrono import",
    "blender": (2, 80, 0),
    "category": "Import-Export",
    "location": "File > Import-Export",
    "description": "Import ProjectChrono simulations",
    "author": "Alessandro Tasora",
    "version": (0, 0, 2),
    "wiki_url": "http://projectchrono.org",
    "doc_url": "http://projectchrono.org",
}

import bpy
import bmesh
import numpy as np
import mathutils
import os
import math
from bpy.types import Operator
from bpy.types import Panel


#
# Globals, to keep things simple with callbacks
#

chrono_frame_objects = None
chrono_assets = None
chrono_frame_assets = None
chrono_materials = []
chrono_frame_materials = []
chrono_images = []
chrono_frame_images = []
chrono_cameras = None
empty_mesh = None
chrono_csys = None
chrono_filename = None
chrono_view_asset_csys = False
chrono_view_asset_csys_size = 0.15
chrono_view_item_csys = False
chrono_view_item_csys_size = 0.25
chrono_view_link_csys = True
chrono_view_link_csys_size = 0.25
chrono_view_materials = True

#
# utility functions to be used in assets.py
#


def make_bsdf_material(nameID, colorRGB, metallic=0, specular=0, specular_tint=0, roughness=0.5, index_refraction=1.450, transmission=0, emissionRGB=(0,0,0,1), emission_strength=1, bump_map=0, bump_height=1.0):
    new_mat = bpy.data.materials.new(name=nameID)

    new_mat.use_nodes = True

    if new_mat.node_tree:
        new_mat.node_tree.links.clear()
        new_mat.node_tree.nodes.clear()
        
    nodes = new_mat.node_tree.nodes
    links = new_mat.node_tree.links
    output = nodes.new(type='ShaderNodeOutputMaterial')
    shader = nodes.new(type='ShaderNodeBsdfPrincipled')
    
    if type(colorRGB) == tuple:
        nodes["Principled BSDF"].inputs[0].default_value = colorRGB
    if type(colorRGB) == str:
        if os.path.exists(colorRGB):
            texturenode = nodes.new(type="ShaderNodeTexImage")
            texturenode.image = bpy.data.images.load(colorRGB)
            links.new(texturenode.outputs[0], nodes["Principled BSDF"].inputs[0])
            
    if type(metallic) == float or type(metallic) == int:
        nodes["Principled BSDF"].inputs[6].default_value = metallic
    if type(metallic) == str:
        if os.path.exists(metallic):
            texturenode = nodes.new(type="ShaderNodeTexImage")
            texturenode.image = bpy.data.images.load(metallic)
            links.new(texturenode.outputs[0], nodes["Principled BSDF"].inputs[6])
            
    nodes["Principled BSDF"].inputs[7].default_value = specular
    nodes["Principled BSDF"].inputs[8].default_value = specular_tint

    if type(roughness) == float or type(roughness) == int:
        nodes["Principled BSDF"].inputs[9].default_value = roughness
    if type(roughness) == str:
        if os.path.exists(roughness):
            texturenode = nodes.new(type="ShaderNodeTexImage")
            texturenode.image = bpy.data.images.load(roughness)
            links.new(texturenode.outputs[0], nodes["Principled BSDF"].inputs[9])
    
    nodes["Principled BSDF"].inputs[16].default_value = index_refraction

    if type(transmission) == float or type(transmission) == int:
        nodes["Principled BSDF"].inputs[17].default_value = transmission
    if type(transmission) == str:
        if os.path.exists(transmission):
            texturenode = nodes.new(type="ShaderNodeTexImage")
            texturenode.image = bpy.data.images.load(transmission)
            links.new(texturenode.outputs[0], nodes["Principled BSDF"].inputs[17])
    
    if type(emissionRGB) == tuple:
        nodes["Principled BSDF"].inputs[19].default_value = emissionRGB
    if type(emissionRGB) == str:
        if os.path.exists(emissionRGB):
            texturenode = nodes.new(type="ShaderNodeTexImage")
            texturenode.image = bpy.data.images.load(emissionRGB)
            links.new(texturenode.outputs[0], nodes["Principled BSDF"].inputs[19])
            
    nodes["Principled BSDF"].inputs[20].default_value = emission_strength
    
    if type(bump_map) == str:
        if os.path.exists(bump_map):
            texturenode = nodes.new(type="ShaderNodeTexImage")
            texturenode.image = bpy.data.images.load(bump_map)
            bumpnode = nodes.new(type="ShaderNodeBump")
            bumpnode.inputs[0].default_value= bump_height
            links.new(texturenode.outputs[0], bumpnode.inputs[2])
            links.new(bumpnode.outputs[0], nodes["Principled BSDF"].inputs[22])
    
    links.new(shader.outputs[0], output.inputs[0])
    
    return new_mat
    

# helper to add scalar attributes as arrays of floats to: faces (mdomain='FACE') or vertexes (mdomain='POINT') of meshes
def add_mesh_data_floats(mesh_object, attribute_scalars, attribute_name='myval', mdomain='FACE'):
    myattr = mesh_object.data.attributes.new(name=attribute_name, type='FLOAT', domain=mdomain)
    iv =0
    for mval in attribute_scalars:
        myattr.data[iv].value = mval
        iv +=1

# helper to add vector attributes as arrays of (x,y,z) to: faces (mdomain='FACE') or vertexes (mdomain='POINT') of meshes
def add_mesh_data_vectors(mesh_object, attribute_vectors, attribute_name='chrono_color', mdomain='FACE'):
    myattr = mesh_object.data.attributes.new(name=attribute_name, type='FLOAT_VECTOR', domain=mdomain)
    iv =0
    for mval in attribute_vectors:
        myattr.data[iv].vector = mval
        iv +=1

# helper function to generate a material that, once assigned to a mesh with a vector attribute, 
# renders it as color (per face or per vertex, depending on the attribute's mesh data domain)
def make_material_mesh_color_attribute(nameID, colname):

    new_mat = bpy.data.materials.new(name=nameID)
    new_mat.use_nodes = True
    if new_mat.node_tree:
        new_mat.node_tree.links.clear()
        new_mat.node_tree.nodes.clear() 
    nodes = new_mat.node_tree.nodes
    links = new_mat.node_tree.links
    output = nodes.new(type='ShaderNodeOutputMaterial')
    shader = nodes.new(type='ShaderNodeBsdfPrincipled')
    links.new(shader.outputs[0], output.inputs[0])
    colattribute = nodes.new(type='ShaderNodeAttribute')
    colattribute.attribute_name = colname
    links.new(colattribute.outputs[0], shader.inputs[0])
    return new_mat
    
# some colormaps used by  make_material_mesh_falsecolor_attribute
colormap_jet = [
 [0,     (0,0,0.56, 1)],
 [0.125, (0,0,1 ,1)   ],
 [0.375, (0,1,1, 1)   ],
 [0.625, (1,1,0, 1 )  ],
 [0.875, (1,0,0, 1 )  ],
 [1,     (0.5,0,0, 1) ]
]
colormap_cooltowarm = [
 [0,         	(0.229999504,	0.298998934,	0.754000139, 1) ],
 [0.142857143,	(0.406839976,	0.537716815,	0.934353077, 1) ],
 [0.285714286,	(0.602704657,	0.731255644,    0.999993038, 1) ],
 [0.428571429,  (0.78841419,	0.845877766,	0.939423093, 1) ],
 [0.571428571,	(0.930635713,	0.820337799,	0.761004578, 1) ], 
 [0.714285714,	(0.967788492,	0.657029313,	0.537326447, 1) ],
 [0.857142857,	(0.88710666,	0.413948424,	0.324564482, 1) ],
 [1	,           (0.706000136,   0.015991824,	0.150000072, 1) ],
]
colormap_viridis = [
 [0,           (0.267003985,    0.004872566,    0.329415069, 1) ],
 [0.142857143, (0.274741032,	0.196973267,	0.497250443, 1) ],
 [0.285714286, (0.212671237,	0.359101377, 	0.551635047, 1) ],
 [0.428571429, (0.152958099,	0.498051451,	0.557685327, 1) ],
 [0.571428571, (0.122053592,	0.632105543,	0.530848866, 1) ],
 [0.714285714, (0.290013937,	0.758845119,	0.427827161, 1) ],
 [0.857142857, (0.622182341,	0.853814293,	0.226247911, 1) ],
 [1,           (0.993248149,	0.906154763,	0.143935944, 1) ]
]
colormap_cool = [
 [0,     (0,1,1, 1)],
 [1,     (1,0,1, 1)]
]
colormap_hot = [
 [0,     (0.039,0,0, 1)],
 [0.375, (1,0,0, 1)    ],
 [0.75,  (1,1,0, 1)    ],
 [1,     (1,1,1, 1)    ]
]
colormap_winter = [
 [0,     (0,0,1, 1)  ],
 [1,     (0,1,0.5, 1)]
]

# helper function to generate a material that, once assigned to a mesh with a float attribute, 
# renders it as falsecolor (per face or per vertex, depending on the attribute's mesh data domain)
def make_material_mesh_falsecolor_attribute(nameID, attrname, attr_min=0.0, attr_max=1.0, colormap='colormap_cooltowarm1'):
    
    new_mat = bpy.data.materials.new(name=nameID)
    new_mat.use_nodes = True
    if new_mat.node_tree:
        new_mat.node_tree.links.clear()
        new_mat.node_tree.nodes.clear()  
    nodes = new_mat.node_tree.nodes
    links = new_mat.node_tree.links
    output = nodes.new(type='ShaderNodeOutputMaterial')
    shader = nodes.new(type='ShaderNodeBsdfPrincipled')
    links.new(shader.outputs[0], output.inputs[0])
    ramp = nodes.new(type='ShaderNodeValToRGB')
    ramp.color_ramp.color_mode = 'HSV'
    for i in range(1,len(colormap)-1):
        ramp.color_ramp.elements.new(colormap[i][0])
    for i in range(0,len(colormap)):
        ramp.color_ramp.elements[i].color = (colormap[i][1]) 
    links.new(ramp.outputs[0], shader.inputs[0])
    maprange = nodes.new(type='ShaderNodeMapRange')
    maprange.inputs[1].default_value = attr_min
    maprange.inputs[2].default_value = attr_max
    links.new(maprange.outputs[0], ramp.inputs[0])
    colattribute = nodes.new(type='ShaderNodeAttribute')
    colattribute.attribute_name = attrname
    links.new(colattribute.outputs[2], maprange.inputs[0])
    return new_mat


#
# utility functions to be used in output/statexxxyy.py files
#

def make_chrono_csys(mpos,mrot, mparent, symbol_scale=0.1):
    csys = bpy.data.objects['chrono_csys']
    csys_obj = csys.copy() # instanced, no full copy, data is shared
    csys_obj.rotation_mode = 'QUATERNION'
    csys_obj.rotation_quaternion = mrot
    csys_obj.location = mpos
    csys_obj.scale = (symbol_scale,symbol_scale,symbol_scale)
    if mparent:
        csys_obj.parent = mparent
    csys_obj.select_set(False)
    return csys_obj


def make_chrono_object_assetlist(mname,mpos,mrot, masset_list):

    chobject = bpy.data.objects.new(mname, empty_mesh)  
    chobject.rotation_mode = 'QUATERNION'
    chobject.rotation_quaternion = mrot
    chobject.location = mpos
    chrono_frame_objects.objects.link(chobject)
    
    if chrono_view_item_csys:
        mcsys = make_chrono_csys((0,0,0),(1,0,0,0), chobject, chrono_view_item_csys_size)
        chrono_frame_objects.objects.link(mcsys)
        
    for m in range(len(masset_list)):
        masset = chrono_assets.objects.get(masset_list[m][0])
        if not masset:
            masset = chrono_frame_assets.objects.get(masset_list[m][0])
        if masset:
            chasset = masset.copy() # instanced, no full copy, masset.data is shared
            chasset.rotation_mode = 'QUATERNION'
            chasset.rotation_quaternion = masset_list[m][2]
            chasset.location = masset_list[m][1]
            if len(masset_list[m])>4:
                chasset.scale = masset_list[m][4]
            chrono_frame_objects.objects.link(chasset)
            chasset.parent = chobject
            if chrono_view_materials:
                while len(chasset.data.materials) < len(masset_list[m][3]):
                    chasset.data.materials.append(None)
                for i, matname in enumerate(masset_list[m][3]):
                    chasset.material_slots[i].link = 'OBJECT'
                    chasset.material_slots[i].material = bpy.data.materials[matname]
            if chrono_view_asset_csys:
                mcsys = make_chrono_csys(chasset.location,chasset.rotation_quaternion, chobject, chrono_view_asset_csys_size)
                chrono_frame_objects.objects.link(mcsys) 
        else:
            print("not found asset: ",masset_list[m][0])
    
    
def make_chrono_object_clones(mname,mpos,mrot, masset_list, list_clones_posrot):
    
    chobject = bpy.data.objects.new(mname, empty_mesh)  
    chobject.rotation_mode = 'QUATERNION'
    chobject.rotation_quaternion = mrot
    chobject.location = mpos
    chrono_frame_objects.objects.link(chobject)
    
    chassets_group = bpy.data.objects.new("assets_group", empty_mesh)  
    chrono_frame_objects.objects.link(chassets_group)
    chassets_group.parent = chobject
    chassets_group.hide_set(True)
    chassets_group.hide_render = True
    
    if chrono_view_item_csys:
        mcsys = make_chrono_csys((0,0,0),(1,0,0,0), chassets_group, chrono_view_item_csys_size)
        chrono_frame_objects.objects.link(mcsys)
    
    for m in range(len(masset_list)):
        masset = chrono_assets.objects.get(masset_list[m][0])
        if not masset:
            masset = chrono_frame_assets.objects.get(masset_list[m][0])
        if masset:
            chasset = masset.copy() # instanced, no full copy, masset.data is shared
            chasset.rotation_mode = 'QUATERNION'
            chasset.rotation_quaternion = masset_list[m][2]
            chasset.location = masset_list[m][1]
            if len(masset_list[m])>4:
                chasset.scale = masset_list[m][4]
            chrono_frame_objects.objects.link(chasset)
            chasset.parent = chassets_group
            if chrono_view_materials:
                while len(chasset.data.materials) < len(masset_list[m][3]):
                    chasset.data.materials.append(None)
                for i, matname in enumerate(masset_list[m][3]):
                    chasset.material_slots[i].link = 'OBJECT'
                    chasset.material_slots[i].material = bpy.data.materials[matname]
            if chrono_view_asset_csys:
                mcsys = make_chrono_csys(chasset.location,chasset.rotation_quaternion, chassets_group, chrono_view_asset_csys_size)
                chrono_frame_objects.objects.link(mcsys)
            chasset.hide_set(True)    
        else:
            print("not found asset: ",masset_list[m][0])
        
    ncl = len(list_clones_posrot)
    verts = [(0,0,0)] * (4*ncl)
    faces = [(0,0,0,0)] * ncl
    edges = []
    for ic in range(ncl):
        mpos = mathutils.Vector(list_clones_posrot[ic][0])
        mrot = mathutils.Quaternion(list_clones_posrot[ic][1])
        verts[4*ic]   = (mpos + mrot @ mathutils.Vector((-0.1,-0.1,0)))[:]
        verts[4*ic+1] = (mpos + mrot @ mathutils.Vector(( 0.1,-0.1,0)))[:]
        verts[4*ic+2] = (mpos + mrot @ mathutils.Vector(( 0.1, 0.1,0)))[:]
        verts[4*ic+3] = (mpos + mrot @ mathutils.Vector((-0.1, 0.1,0)))[:]
        faces[ic] = (4*ic, 4*ic+1, 4*ic+2, 4*ic+3)    
    new_mesh = bpy.data.meshes.new('mesh_position_clones')
    new_mesh.from_pydata(verts, edges, faces)
    new_mesh.update()
    chobject.data = new_mesh
    chobject.instance_type = 'FACES'
    chobject.show_instancer_for_render = False
    chobject.show_instancer_for_viewport = False
    
def update_camera_coordinates(mname,mpos,mrot):
    cameraasset = chrono_cameras.objects.get(mname)
    cameraasset.rotation_mode = 'QUATERNION'
    cameraasset.rotation_quaternion = mrot
    cameraasset.location = mpos

 
    
#
# On file selected: 
#
# RESET CHRONO OBJECTS COLLECTION
# LOAD OBJECTS
#

def callback_post(self):

    scene = bpy.context.scene
    cFrame = scene.frame_current
    sFrame = scene.frame_start
    
    global chrono_frame_objects
    global chrono_assets
    global chrono_frame_assets
    global chrono_materials
    global chrono_frame_materials
    global chrono_images
    global chrono_frame_images
    global chrono_cameras
    global chrono_csys
    global empty_mesh
    global chrono_filename
    global chrono_view_asset_csys
    global chrono_view_asset_csys_size
    global chrono_view_item_csys
    global chrono_view_item_csys_size
    global chrono_view_link_csys
    global chrono_view_link_csys_size
    global chrono_view_materials
    
    chrono_assets = bpy.data.collections.get('chrono_assets')
    chrono_frame_assets = bpy.data.collections.get('chrono_frame_assets')
    chrono_frame_objects = bpy.data.collections.get('chrono_frame_objects')
    chrono_cameras = bpy.data.collections.get('chrono_cameras')
    
    if (chrono_assets and chrono_frame_objects and chrono_frame_assets):
        
        # retrieve filename of last import, in case this was a saved Blender project
        chrono_filename = chrono_assets['chrono_filename'] 
        
        # delete all things in chrono_frame_objects collection
        for obj in chrono_frame_objects.objects:
            bpy.data.objects.remove(obj, do_unlink=True)
            
        # delete all things in chrono_frame_assets collection
        for obj in chrono_frame_assets.objects:
            bpy.data.objects.remove(obj, do_unlink=True)
          
        # delete unused materials, meshes, etc.
        orphan_mesh = [m for m in bpy.data.meshes if not m.users]
        while orphan_mesh:
            bpy.data.meshes.remove(orphan_mesh.pop())
            
        orphan_particles = [m for m in bpy.data.particles if not m.users]
        while orphan_particles:
            bpy.data.particles.remove(orphan_particles.pop())
        
        # remove just materials added as per-frame materials    
        for mmat in chrono_frame_materials:
            bpy.data.materials.remove(mmat)
        chrono_frame_materials = [] # empty list 
            
        #orphan_materials = [m for m in bpy.data.materials if not m.users]
        #while orphan_materials:
        #    bpy.data.materials.remove(orphan_materials.pop())
            
        orphan_image = [m for m in bpy.data.images if not m.users]
        while orphan_image:
            bpy.data.images.remove(orphan_image.pop())
        
        # load state file, that will fill the chrono_frame_objects and (if necessary) 
        # the chrono_frame_assets collections
        
        proj_dir = os.path.dirname(os.path.abspath(chrono_filename))
        filename = os.path.join(proj_dir, 'output', 'state'+'{:05d}'.format(cFrame)+'.py')
        
        if os.path.exists(filename):
            f = open(filename, "rb")
            exec(compile(f.read(), filename, 'exec'))
            f.close()
            
        # in case something was added to chrono_frame_assets, make it invisible 
        for masset in chrono_frame_assets.objects:
            masset.hide_set(True) # not masset.hide_viewport = True otherwise also instances are invisible

     
#
# On file selected: 
#
# PREPARE CHRONO CAMERAS COLLECTION
# PREPARE CHRONO ASSETS COLLECTION
# PREPARE CHRONO FRAME ASSETS COLLECTION
# PREPARE CHRONO OBJECTS COLLECTION
# LOAD ASSETS (NON MUTABLE)
#
    
def read_chrono_simulation(context, filepath, setting_materials):
    print("Loading Chrono simulation...")
    
    # PREPARE SCENE
    global chrono_frame_objects
    global chrono_assets
    global chrono_frame_assets
    global chrono_materials
    global chrono_frame_materials
    global chrono_images
    global chrono_frame_images
    global chrono_cameras
    global chrono_csys
    global empty_mesh
    global chrono_filename
    global chrono_view_asset_csys
    global chrono_view_asset_csys_size
    global chrono_view_item_csys
    global chrono_view_item_csys_size
    global chrono_view_link_csys
    global chrono_view_link_csys_size
    global chrono_view_materials

    chrono_cameras = bpy.data.collections.get('chrono_cameras')
    if not chrono_cameras:
        chrono_cameras = bpy.data.collections.new('chrono_cameras')
        bpy.context.scene.collection.children.link(chrono_cameras)
    for obj in chrono_cameras.objects:
            bpy.data.objects.remove(obj, do_unlink=True)
            
    chrono_assets = bpy.data.collections.get('chrono_assets')
    if not chrono_assets:
        chrono_assets = bpy.data.collections.new('chrono_assets')
        bpy.context.scene.collection.children.link(chrono_assets)
    for obj in chrono_assets.objects:
            bpy.data.objects.remove(obj, do_unlink=True)
            
    chrono_assets.hide_render = True
    #chrono_assets.hide_viewport = True
    
    chrono_frame_assets = bpy.data.collections.get('chrono_frame_assets')
    if not chrono_frame_assets:
        chrono_frame_assets = bpy.data.collections.new('chrono_frame_assets')
        bpy.context.scene.collection.children.link(chrono_frame_assets)
    for obj in chrono_frame_assets.objects:
            bpy.data.objects.remove(obj, do_unlink=True)
            
    chrono_frame_assets.hide_render = True
    #chrono_frame_assets.hide_viewport = True
            
    chrono_frame_objects = bpy.data.collections.get('chrono_frame_objects')
    if not chrono_frame_objects:
        chrono_frame_objects = bpy.data.collections.new('chrono_frame_objects')
        bpy.context.scene.collection.children.link(chrono_frame_objects)
    for obj in chrono_frame_objects.objects:
            bpy.data.objects.remove(obj, do_unlink=True)
            
    orphan_mesh = [m for m in bpy.data.meshes if not m.users]
    while orphan_mesh:
        bpy.data.meshes.remove(orphan_mesh.pop())
        
    orphan_particles = [m for m in bpy.data.particles if not m.users]
    while orphan_particles:
        bpy.data.particles.remove(orphan_particles.pop())

    # remove materials added as per-frame materials    
    for mmat in chrono_frame_materials:
        bpy.data.materials.remove(mmat)
    chrono_frame_materials = [] # empty list
    # remove materials added as immutable materials    
    for mmat in chrono_materials:
        bpy.data.materials.remove(mmat)
    chrono_materials = [] # empty list
               
    orphan_image = [m for m in bpy.data.images if not m.users]
    while orphan_image:
        bpy.data.images.remove(orphan_image.pop())
           
    # if some sub collection is selected, select the root one, so loading assets.py will work fine  
    bpy.context.view_layer.active_layer_collection = bpy.context.view_layer.layer_collection
       
    # create template for all chrono physics items
    empty_mesh = bpy.data.meshes.get('empty_mesh')
    if not empty_mesh:
        empty_mesh = bpy.data.meshes.new('empty_mesh')
        empty_mesh.from_pydata([], [], []) 
        empty_mesh_object = bpy.data.objects.new('empty_mesh_object', empty_mesh)

    # create template for all chrono coordinate systems
    chrono_csys = bpy.data.objects.get('chrono_csys')
    if not chrono_csys:
        bpy.ops.mesh.primitive_cylinder_add(vertices=10, radius=0.02, depth=1.0, calc_uvs=False, rotation=(0,0,0),location=(0,0,0.5))
        cyZ = bpy.context.object
        matZ = make_bsdf_material('axis_z',(0,0,1,1)) #for render
        matZ.diffuse_color = (0,0,1,1) #for viewport  
        cyZ.data.materials.append(matZ) 
        bpy.ops.mesh.primitive_cylinder_add(vertices=10, radius=0.02, depth=1.0, calc_uvs=False, rotation=(math.pi/2.0,0,0),location=(0,0.5,0))
        cyY = bpy.context.object
        matY = make_bsdf_material('axis_y',(0,1,0,1)) #for render
        matY.diffuse_color = (0,1,0,1) #for viewport
        cyY.data.materials.append(matY)
        bpy.ops.mesh.primitive_cylinder_add(vertices=10, radius=0.02, depth=1.0, calc_uvs=False, rotation=(0,math.pi/2.0,0),location=(0.5,0,0))
        cyX = bpy.context.object
        matX = make_bsdf_material('axis_x',(1,0,0,1)) #for render
        matX.diffuse_color = (1,0,0,1) #for viewport
        cyX.data.materials.append(matX)
        bpy.ops.mesh.primitive_cube_add(size=0.1,calc_uvs=False, location=(0,0,0))
        cyO = bpy.context.object
        matO = make_bsdf_material('axis_origin',(1,1,1,1)) #for render
        matO.diffuse_color = (1,1,1,1) #for viewport
        cyO.data.materials.append(matO)
        with bpy.context.temp_override(selected_editable_objects=[cyZ,cyY,cyX,cyO]):
            bpy.ops.object.join()
        chrono_csys = bpy.context.object
        with bpy.context.temp_override(selected_editable_objects=[chrono_csys]):
            bpy.ops.object.shade_smooth(use_auto_smooth=True,auto_smooth_angle=1.1)
        chrono_csys.visible_shadow = False
        chrono_csys.name ='chrono_csys'
        chrono_csys.select_set(False)
        bpy.context.scene.collection.objects.unlink(chrono_csys)
    

    
    chrono_filename = filepath
    
    # store filename as custom properties of collection, so that one can save the Blender project,
    # reopen, and scrub the timeline without the need of doing File/Import/Chrono Import
    chrono_assets['chrono_filename'] = chrono_filename
    
    # Load the xxx.assets.py file to create the non-mutable assets 
    # that will fill the chrono_assets collection.
    # This is executed once per each import. 
    f = open(chrono_filename, "rb")
    exec(compile(f.read(), chrono_filename, 'exec'))
    f.close()
    
    print('chrono_view_item_csys=',chrono_view_item_csys)
    
    # in case something was added to chrono_assets, make it invisible
    for masset in chrono_assets.objects:
        masset.hide_set(True) # not masset.hide_viewport = True otherwise also instances are invisible

    degp = bpy.context.evaluated_depsgraph_get()

    bpy.context.scene.chrono_show_assets_coordsys = chrono_view_asset_csys
    bpy.context.scene.chrono_assets_coordsys_size = chrono_view_asset_csys_size
    bpy.context.scene.chrono_show_item_coordsys = chrono_view_item_csys
    bpy.context.scene.chrono_item_coordsys_size = chrono_view_item_csys_size
    bpy.context.scene.chrono_show_links_coordsys = chrono_view_link_csys
    bpy.context.scene.chrono_links_coordsys_size = chrono_view_link_csys_size
    bpy.context.scene.chrono_show_materials = chrono_view_materials
    
    #clear the post frame handler
    bpy.app.handlers.frame_change_post.clear()

    #run the function on each frame
    bpy.app.handlers.frame_change_post.append(callback_post)


    # TEST: Update to a frame where particles are updated
    bpy.context.scene.frame_current = 0


    return {'FINISHED'}



# Sidebar GUI ----------------------------------------------



def UpdatedFunction(self, context):
    global chrono_view_asset_csys
    global chrono_view_asset_csys_size
    global chrono_view_item_csys
    global chrono_view_item_csys_size
    global chrono_view_link_csys
    global chrono_view_link_csys_size
    global chrono_view_materials
    print("In update func...")
    if True:
        chrono_view_asset_csys = self.chrono_show_assets_coordsys
        chrono_view_asset_csys_size = self.chrono_assets_coordsys_size
        chrono_view_item_csys = self.chrono_show_item_coordsys
        chrono_view_item_csys_size = self.chrono_item_coordsys_size
        chrono_view_links_csys = self.chrono_show_links_coordsys
        chrono_view_links_csys_size = self.chrono_links_coordsys_size
        chrono_view_materials = self.chrono_show_materials
        callback_post(self) # force reload 
    
class Chrono_operator(Operator):
    """ example operator """
    bl_idname = "demo.operator"
    bl_label = "I'm a Skeleton Operator"
    bl_options = {"REGISTER", "UNDO"}

    @classmethod
    def poll(cls, context):
        return context.mode == "OBJECT"

    def execute(self, context):

        self.report({'INFO'},
            f"execute()")

        return {'FINISHED'}


class Chrono_sidebar(Panel):
    """Chrono settings for 3D view"""
    bl_label = "Chrono view"
    bl_space_type = "VIEW_3D"
    bl_region_type = "UI"
    bl_category = "Chrono"

    def draw(self, context):
        col = self.layout.column(align=True)
        #prop = col.operator(TLA_OT_operator.bl_idname, text="Test")
        
        col.prop(context.scene, "chrono_show_item_coordsys")
        col.prop(context.scene, "chrono_item_coordsys_size")
        col.prop(context.scene, "chrono_show_assets_coordsys")
        col.prop(context.scene, "chrono_assets_coordsys_size")
        col.prop(context.scene, "chrono_show_links_coordsys")
        col.prop(context.scene, "chrono_links_coordsys_size")
        col.prop(context.scene, "chrono_show_materials")
 
sidebar_classes = [
    Chrono_operator,
    Chrono_sidebar,
]






# Menu in File/Import/.. GUI ----------------------------------------------


# ImportHelper is a helper class, defines filename and
# invoke() function which calls the file selector.
from bpy_extras.io_utils import ImportHelper
from bpy.props import StringProperty, BoolProperty, EnumProperty, IntProperty
from bpy.types import Operator


class ImportChrono(Operator, ImportHelper):
    """Import a Chrono simulation from files saved by Chrono postprocessing module"""
    bl_idname = "import_chrono.data"  # important since its how bpy.ops.import_chrono.data is constructed
    bl_label = "Import Chrono simulation"

    # ImportHelper mixin class uses this
    filename_ext = ".py"

    filter_glob: StringProperty(
        default="*.assets.py",
        options={'HIDDEN'},
        maxlen=255,  # Max internal buffer length, longer would be clamped.
    )

    # List of operator properties, the attributes will be assigned
    # to the class instance from the operator settings before calling.
    setting_materials: BoolProperty(
        name="Create materials",
        description="Turn off if you want to skip all materials assigned from Chrono side",
        default=True,
    )
    #    setting_from: IntProperty(
    #        name="from",
    #        description="Initial frame",
    #        min = 0,
    #        default=0,
    #    )
    #    setting_to: IntProperty(
    #        name="to",
    #        description="Final frame",
    #        min = 0,
    #        default=0,
    #    )
    
    def execute(self, context):
        return read_chrono_simulation(context, self.filepath, self.setting_materials)


# Only needed if you want to add into a dynamic menu.
def menu_func_import(self, context):
    self.layout.operator(ImportChrono.bl_idname, text="Chrono import")



# REGISTER THE ADD-ON ----------------------------------------------


def register():
    
    # this is needed to avoid crashes when pressing F12 for rendering  
    bpy.context.scene.render.use_lock_interface = True
    
    bpy.utils.register_class(ImportChrono)
    bpy.types.TOPBAR_MT_file_import.append(menu_func_import)
    
        #clear the post frame handler
    bpy.app.handlers.frame_change_post.clear()

    #run the function on each frame
    bpy.app.handlers.frame_change_post.append(callback_post)

    # sidebar UI:
    for c in sidebar_classes:
        bpy.utils.register_class(c)
        
    bpy.types.Scene.chrono_show_item_coordsys = bpy.props.BoolProperty(
        name='Show items coordsys',
        default=False,
        update=UpdatedFunction
    )
    bpy.types.Scene.chrono_item_coordsys_size = bpy.props.FloatProperty(
        name='Item coordsys size',
        default=0.15,
        update=UpdatedFunction
    )
    bpy.types.Scene.chrono_show_assets_coordsys = bpy.props.BoolProperty(
        name='Show assets coordsys',
        default=False,
        update=UpdatedFunction
    )
    bpy.types.Scene.chrono_assets_coordsys_size = bpy.props.FloatProperty(
        name='Assets coordsys size',
        default=0.10,
        update=UpdatedFunction
    )
    bpy.types.Scene.chrono_show_links_coordsys = bpy.props.BoolProperty(
        name='Show links coordsys',
        default=True,
        update=UpdatedFunction
    )
    bpy.types.Scene.chrono_links_coordsys_size = bpy.props.FloatProperty(
        name='Links coordsys size',
        default=0.20,
        update=UpdatedFunction
    )
    bpy.types.Scene.chrono_show_materials = bpy.props.BoolProperty(
        name='Use Chrono materials',
        default=True,
        update=UpdatedFunction
    )
    

def unregister():
    
    del bpy.types.Scene.chrono_show_item_coordsys
    del bpy.types.Scene.chrono_item_coordsys_size
    del bpy.types.Scene.chrono_show_assets_coordsys
    del bpy.types.Scene.chrono_assets_coordsys_size
    del bpy.types.Scene.chrono_show_links_coordsys
    del bpy.types.Scene.chrono_links_coordsys_size
    del bpy.types.Scene.chrono_show_materials
    for c in sidebar_classes:
        bpy.utils.unregister_class(c)
        
        
    bpy.utils.unregister_class(ImportChrono)
    bpy.types.TOPBAR_MT_file_import.remove(menu_func_import)
    bpy.app.handlers.frame_change_post.remove(callback_post)

    # sidebar UI:
    



if __name__ == "__main__":
    register()

    # TEST: test call
    #bpy.ops.import_chrono.data('INVOKE_DEFAULT')
