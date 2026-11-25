"""Reference trajectories in the neutral wheel's frame, in radii and meters.

The wheel hierarchy is from the FH6 R30 car that references the global Mojo
wheel clips. It is verified against bone IDs, not sorted by name. The driver
hierarchy is the original matching character skeleton decoded from FH6.
"""
import math

import read_mojo as reader


def multiply(a,b):
    x,y,z,w=a
    X,Y,Z,W=b
    return (w*X+x*W+y*Z-z*Y, w*Y+y*W+z*X-x*Z,
            w*Z+z*W+x*Y-y*X, w*W-x*X-y*Y-z*Z)


def inverse(q):
    return (-q[0],-q[1],-q[2],q[3])


def rotate(q,xyz):
    return multiply(multiply(q,(*xyz,0)),inverse(q))[:3]


def world_pose(name,frame,bones,parents):
    path=[]
    while name:
        path.append(name)
        name=parents[name]
    pos=(0,0,0)
    q=(0,0,0,1)
    for bone_name in reversed(path):
        local=bones[bone_name][frame]
        offset=rotate(q,local[:3])
        pos=tuple(v+d for v,d in zip(pos,offset))
        q=multiply(q,local[3:7])
        length=math.sqrt(sum(v*v for v in q))
        q=tuple(v/length for v in q)
    return pos,q


def wheel_parents(z,clip,rig_bytes):
    keys=reader.names(z,clip)
    _, root=reader.root_fields(rig_bytes)
    (_,_,_,body),=reader.pairs(root[1])
    fields=reader.fields(body)
    bone_ids=[key for key, in reader.vector(fields[0],"Q")]
    parents=[parent for parent, in reader.vector(fields[1],"h")]
    return {keys[key]:keys[bone_ids[parents[i]]] if parents[i]>=0 else None
            for i,key in enumerate(bone_ids) if key in keys and (parents[i]<0 or bone_ids[parents[i]] in keys)}


def reference(donor,radius_m,wheel_tree):
    char, wheel=donor["character"], donor["wheel"]
    char_tree={n:rec["parent"] for n,rec in donor["skeleton"]["bones"].items()}
    assert all(n in wheel_tree for n in wheel["frames_by_bone"])
    neutral=donor["wheel_phase"]["center_frame"]
    center,orientation=world_pose("boneSteeringWheel",neutral,wheel["frames_by_bone"],wheel_tree)
    result=[]
    for i,deg in enumerate(donor["wheel_phase"]["degrees_by_frame"]):
        coords={}
        for part,tree,names in [(char,char_tree,("LeftHand","RightHand","LeftForeArm","RightForeArm")),
                                (wheel,wheel_tree,("anchor_lefthand","anchor_righthand"))]:
            for name in names:
                point,_=world_pose(name,i,part["frames_by_bone"],tree)
                disp=rotate(inverse(orientation),tuple(x-y for x,y in zip(point,center)))
                coords[name]=[round(x/radius_m,6) for x in disp]
        result.append({"frame":i,"input_right":round(-deg/abs(donor["wheel_phase"]["degrees_by_frame"][0]),6),
                       "wheel_local_z_degrees":deg, "xyz_radii":coords})
    errors={}
    for side in ("Left","Right"):
        def err(row):
            coords=row["xyz_radii"]
            return radius_m*math.dist(coords[side+"Hand"],coords["anchor_"+side.lower()+"hand"])
        errors[side]={"mean_m":sum(err(r) for r in result)/len(result),"max_m":max(err(r) for r in result)}
    return {"wheel_center_world_m":center,"wheel_orientation_world_xyzw":orientation,
            "units":"coordinates in wheel radii in the fixed neutral physical wheel frame; multiply by radius_m for meters",
            "radius_m":radius_m,"center_frame":neutral,"wheel_parent_skeleton":wheel_tree,
            "wrist_to_anchor_check":errors,"frames":result}

