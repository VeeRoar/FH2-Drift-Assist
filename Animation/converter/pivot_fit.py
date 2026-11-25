"""Recover a legacy steering-wheel axis line from both stock grips when necessary.

fit_pivot(left, right, angle, centre, frame) returns (world_centre, diagnostics).
left/right are (N,3) samples of a palm contact proxy in the unchanged original
steering clip. angle is the corresponding signed wheel rotation in radians;
the middle sample is neutral. frame columns are orthonormal world right, up,
normal. Coordinates use metres. Only the two wheel-plane coordinates can be
identified from rotation; the original depth along the axle is preserved.
"""
import numpy as np


def _solve(xy, theta):
    count=len(theta)
    rotation=np.exp(1j*theta)
    design=np.zeros((2*count,3),dtype=complex)
    design[:,0]=1.
    design[:count,1]=rotation
    design[count:,2]=rotation
    pts=xy.reshape(-1)
    weights=np.ones(len(pts))
    for _ in range(5):
        w=np.sqrt(weights)
        fit=np.linalg.lstsq(design*w[:,None],pts*w,rcond=None)[0]
        error=np.abs(design @ fit-pts)
        cutoff=max(.006,1.6*np.median(error))
        weights=np.minimum(1.,cutoff/np.maximum(error,1e-9))
    rms=float(np.sqrt(np.average(error*error,weights=weights)))
    # If stock centre were right, an independent optimal material grip
    # could be chosen per hand in the rotating frame without an intercept.
    uw=np.tile(np.conj(rotation),2)
    grip=np.average((pts*uw).reshape(2,count),axis=1,weights=None)
    fixed_error=np.abs(xy-grip[:,None]*rotation)
    rms_at_asset=float(np.sqrt(np.average(fixed_error.reshape(-1)**2,weights=weights)))
    return fit[0],np.abs(fit[1:]),rms,rms_at_asset


def fit_pivot(left,right,angle,centre,frame):
    left,right,angle,centre,frame=(np.asarray(x,dtype=float) for x in (left,right,angle,centre,frame))
    n=len(angle)
    if (angle.shape!=(n,) or n<5 or left.shape!=(n,3) or right.shape!=(n,3) or
            centre.shape!=(3,) or frame.shape!=(3,3) or
            any(not np.isfinite(x).all() for x in (left,right,angle,centre,frame))):
        raise ValueError("Expected finite stock grips, wheel angles, centre and basis")
    if not np.allclose(frame.T @ frame,np.eye(3),atol=1e-4) or np.linalg.det(frame)<.999:
        raise ValueError("Wheel basis must be a right-handed orthonormal matrix with columns as axes")
    projected=(np.stack((left,right))-centre) @ frame[:,:2]
    coords=projected[:,:,0]+1j*projected[:,:,1]
    mid=n//2
    sweep=np.unwrap(angle)
    sweep-=sweep[mid]
    rotations=float(np.ptp(sweep))
    halfspan=float(abs(coords[1,mid]-coords[0,mid])*.5)
    normal=frame[:,2]
    if not np.allclose(np.linalg.norm(normal),1.,atol=1e-4):
        raise ValueError("Wheel normal must have unit length")
    if rotations<np.deg2rad(30.) or halfspan<.045:
        return centre.copy(),dict(applied=False,reason="insufficient steering arc or narrow special control",
                                  sweep_deg=float(np.rad2deg(rotations)),halfspan_m=halfspan)

    ahead,reverse=_solve(coords,sweep),_solve(coords,-sweep)
    sign=1 if ahead[2]<=reverse[2] else -1
    best,other=(ahead,reverse) if sign==1 else (reverse,ahead)
    center2,radii,rms,old_rms=best
    # The known 5 bad axes are hundreds of mm to metres wrong. Ordinary
    # source-hand animation can itself deviate from a rigid arc by ~25 mm.
    strong_sign=other[2]>max(1.4*rms,rms+.012)
    inside=np.all((radii>halfspan*.45)&(radii<halfspan*2.6))
    reliable=strong_sign and inside and rms<max(.035,halfspan*.20)
    separated=abs(center2)>max(.055,halfspan*.25,2.2*rms)
    improved=old_rms>max(1.5*rms,rms+.014)
    apply=bool(reliable and separated and improved)
    shift=np.array([center2.real,center2.imag])
    corrected=centre+frame[:,:2]@shift if apply else centre.copy()
    details=dict(applied=apply,reason=("stock grip orbit" if apply else "native centre retained"),
                 fitted_offset_m=shift.tolist(),offset_m=float(abs(center2)),
                 grip_radii_m=radii.tolist(),halfspan_m=halfspan,sweep_deg=float(np.rad2deg(rotations)),
                 input_angle_sign=sign,sign_clear=bool(strong_sign),
                 fit_rms_m=rms,asset_rms_m=old_rms,opposite_sign_rms_m=other[2])
    return corrected,details
