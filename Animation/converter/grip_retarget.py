"""Per-car FH6 steering animation with the grip calibrated about the true rim."""
from functools import lru_cache
import numpy as np

from retarget import SIDES, SPINDLE, Pose, ClipSampler, unit, tx, apply, matrix, quats, swing, rewrite_clip
from pivot_fit import fit_pivot

def digit_names(side):
    return [side+"Hand"+d+"2" for d in ("Index","Middle","Ring")]


def spin_z(angle):
    c,s=np.cos(angle),np.sin(angle)
    return np.array([[c,-s,0],[s,c,0],[0,0,1.]])


def donor_grip(source,side):
    names=[source.names[k.lower()] for k in digit_names(side)]
    world=np.mean([source.pose.world(k)[1] for k in names],axis=0)
    # The reference wrist positions are given in wheel radii, the raw
    # character positions in metres. This recovers the donor pivot in
    # character space even for a wheel profile with a different tilt.
    hand=source.pose.world('LeftHand')[1][source.center]
    other=source.pose.world('RightHand')[1][source.center]
    left=source.points['LeftHand'][source.center]
    right=source.points['RightHand'][source.center]
    radius=np.linalg.norm(other-hand)/np.linalg.norm(right-left)
    origin=hand-source.frame@left*radius
    return (world-origin)@source.frame


def convert(raw, driver_rig, source):
    n, center=len(source.phases), source.center
    parents={k:v["parent"] for k,v in driver_rig.items()}
    with ClipSampler(raw) as clip:
        @lru_cache(None)
        def local(name):
            if name in clip.tracks:
                samples=[clip.pose(name,float(t)) for t in source.phases]
                return matrix([s[0] for s in samples]),np.array([s[1] for s in samples])
            b=driver_rig[name]
            return (np.broadcast_to(matrix(b["rotation"]),(n,3,3)),
                    np.broadcast_to(b["translation"],(n,3)))

        old, new=Pose(parents,local),Pose(parents,local)
        original = lambda bone: old.world(bone)[1][center]
        left, right = original("LeftHand"),original("RightHand")
        # Some GR2 drivers have a rotated root or a spindle with a different
        # local axis. Recover the plane in driver space from its original
        # moving grips, with the front-facing normal selected by sweep order.
        spans=old.world("RightHand")[1]-old.world("LeftHand")[1]
        height=spans[n//4]-spans[-1-n//4]
        hand_right=unit(right-left)
        trace_normal=unit(np.cross(hand_right,height))
        spindle,_=local(SPINDLE)
        end_rel=quats(spindle[center].T @ spindle)[-1]
        if end_rel[3]<0: end_rel=-end_rel
        sense=unit(end_rel[:3])
        axis=old.world(SPINDLE)[0][center] @ sense
        axis=axis*np.sign(axis @ trace_normal)
        horizontal=unit(hand_right-axis*(axis @ hand_right))
        # Most target rigs are already in car space (X right). If the
        # neutral hands confirm this convention, use true rim horizontal so
        # the driver's slight difference in grip height is preserved.
        side_axis=np.array([1.,0,0])
        car_right=side_axis-axis*(side_axis @ axis)
        if np.linalg.norm(car_right)>.1:
            car_right=unit(car_right)
            alignment=horizontal @ car_right
            if abs(alignment)>.985:
                horizontal=car_right*np.sign(alignment)
        vertical=unit(np.cross(axis,horizontal))
        target_frame=np.column_stack((horizontal,vertical,axis))
        stock_grips={side:np.mean([old.world(k)[1] for k in digit_names(side)],axis=0) for side in SIDES}
        spindle_world,spindle_points=old.world(SPINDLE)
        marker=apply(spindle_world,spindle_world[center].T @ horizontal)
        angle=np.arctan2(marker @ vertical,marker @ horizontal)
        pivot,pivot_note=fit_pivot(stock_grips["Left"],stock_grips["Right"],angle,
                                  spindle_points[center],target_frame)
        radius=float((right-left) @ horizontal/2)
        if not .045<radius<.48:
            raise ValueError(f"Original steering grip spans {radius*2:.3f} m")
        rotations={}
        translations={}
        errors={}
        grip_errors={}
        grip_angles={}

        def mapped_point(oldname, sourcename):
            p=source.points[sourcename]
            return original(oldname)+apply(target_frame,(p-p[center])*radius)

        for side in SIDES:
            arm,fore,hand=(side+x for x in ("Arm","Forearm","Hand"))
            native=donor_grip(source,side)
            ref=(stock_grips[side][center]-pivot) @ target_frame
            phase=np.arctan2(ref[1],ref[0])-np.arctan2(native[center,1],native[center,0])
            grip_angles[side]=float(np.rad2deg(np.angle(np.exp(1j*phase))))
            gain=np.linalg.norm(ref[:2])/np.linalg.norm(native[center,:2])
            if not .45<gain<2.2:
                raise ValueError(f"Implausible {side} grip scale {gain:.3f}")
            hand_frame=target_frame @ spin_z(phase)
            change_basis=hand_frame @ source.frame.T
            # In-plane mapping is a similarity about the recipient's real
            # pivot, with its own grip phase and radius. On the normal use
            # stock grip depth plus the donor's actual lift during releases.
            local_target=apply(spin_z(phase),native)*gain
            local_target[:,2]+=ref[2]-local_target[center,2]
            grasp=pivot+apply(target_frame,local_target)

            @lru_cache(None)
            def desired_rotation(k):
                src=source.pose.world(source.names[k.lower()])[0]
                return change_basis @ (src @ src[center].T) @ change_basis.T @ old.world(k)[0][center]

            @lru_cache(None)
            def palm_relative(k):
                if k==hand:
                    return desired_rotation(hand),np.zeros((n,3))
                pq,pp=palm_relative(parents[k])
                q,t=local(k)
                rotation=desired_rotation(k) if k.lower() in source.names and k in clip.tracks else pq @ q
                return rotation,pp+apply(pq,t)

            offsets=np.mean([palm_relative(k)[1] for k in digit_names(side)],axis=0)
            want=grasp-offsets
            pole=mapped_point(fore,side+"ForeArm")
            # The stock clip holds each driver's seat position and torso
            # movement. Solve the two links at their original lengths.
            u0=old.world(fore)[1]-old.world(arm)[1]
            v0=old.world(hand)[1]-old.world(fore)[1]
            length_u=np.linalg.norm(u0,axis=1)
            length_v=np.linalg.norm(v0,axis=1)
            # When a long reach would straighten the elbow, rotate the
            # collarbone toward the grip before solving the arm. This keeps
            # the original limb lengths and the wheel-side wrist position.
            shoulder=side+"Shoulder"
            shoulder_q,socket=new.world(shoulder)
            lever=old.world(arm)[1]-old.world(shoulder)[1]
            clav_len=np.linalg.norm(lever,axis=1)
            toward=unit(want-socket)
            d=np.linalg.norm(want-socket,axis=1)
            L=length_u+length_v-3e-5
            cosine=np.clip((unit(lever)*toward).sum(axis=1),-1,1)
            needed=np.clip((d*d+clav_len*clav_len-L*L)/(2*d*clav_len),-1,1)
            old_angle=np.arccos(cosine)
            angle=np.maximum(old_angle-np.deg2rad(30),np.minimum(np.arccos(needed),old_angle))
            outer=unit(lever-toward*((lever*toward).sum(axis=1))[:,None])
            newlever=toward*np.cos(angle)[:,None]+outer*np.sin(angle)[:,None]
            new.set_world(shoulder,swing(lever,newlever) @ shoulder_q,rotations)
            # A few deeply reclined driving poses need a little scapular
            # protraction at full reach. Translate the girdle only by the
            # remaining error after the available collar rotation.
            upper_pos=socket+clav_len[:,None]*newlever
            remaining=np.maximum(0,np.linalg.norm(want-upper_pos,axis=1)-L)
            if remaining.max()>.0002:
                shift=unit(want-upper_pos)*np.minimum(remaining,.05)[:,None]
                translations[shoulder]=local(shoulder)[1]+apply(tx(new.world(parents[shoulder])[0]),shift)
                new.position[shoulder]=socket+shift
            a=new.world(arm)[1]
            ray=unit(want-a)
            reach=np.clip(np.linalg.norm(want-a,axis=1),abs(length_u-length_v)+1e-6,length_u+length_v-1e-6)
            along=(length_u**2-length_v**2+reach**2)/(2*reach)
            radial=pole-a-ray*((pole-a)*ray).sum(axis=1)[:,None]
            b=a+along[:,None]*ray+np.sqrt(np.maximum(0,length_u**2-along**2))[:,None]*unit(radial)
            goal=a+reach[:,None]*ray
            for k,oldvec,vec in ((arm,u0,b-a),(fore,v0,goal-b)):
                ref=desired_rotation(k)
                direction=apply(tx(old.world(k)[0]),oldvec)
                q=swing(apply(ref,direction),vec) @ ref
                new.set_world(k,q,rotations)
                if k==arm and side+"ArmHelper" in clip.tracks:
                    helper=side+"ArmHelper"
                    new.set_world(helper,q @ tx(old.world(k)[0]) @ old.world(helper)[0],rotations)
            new.set_world(hand,desired_rotation(hand),rotations)
            error=np.linalg.norm(new.world(hand)[1]-want,axis=1)
            errors[side]=[float(np.mean(error)),float(np.max(error))]
            # Match finger articulation in world space relative to each
            # hand's centre grip; old skeletons insert an extra FingerBase.
            for k in driver_rig:
                if k.startswith(side+"Hand") and k!=hand and k.lower() in source.names and k in clip.tracks:
                    new.set_world(k,desired_rotation(k),rotations)
            seen=np.mean([new.world(k)[1] for k in digit_names(side)],axis=0)
            grip_errors[side]=[float(np.mean(np.linalg.norm(seen-grasp,axis=1))),
                               float(np.max(np.linalg.norm(seen-grasp,axis=1)))]

        half=np.deg2rad(source.degrees[center]-source.degrees)/2
        turn=np.column_stack((np.sin(half)[:,None]*sense,np.cos(half)))
        rotations[SPINDLE]=quats(spindle[center] @ matrix(turn))
        info={"radius":radius,"wrist_errors_m":errors,"tracks_retargeted":len(rotations),
              "grip_errors_m":grip_errors,"palm_phase_degrees":grip_angles,"pivot":pivot_note}
    return rewrite_clip(raw,rotations=rotations,translations=translations,phases=source.phases),info
