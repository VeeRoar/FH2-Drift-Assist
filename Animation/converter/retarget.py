"""Fit decoded FH6 steering poses to a car's original FH2/FM6 driver clip."""
import numpy as np

from gr2_pose import ClipSampler, rewrite_clip

SIDES = ("Left", "Right")
SPINDLE = "boneSteeringWheelSpindle"


def unit(v):
    norm = np.linalg.norm(v, axis=-1, keepdims=True)
    if np.any(norm < 1e-8):
        raise ValueError("Degenerate rig vector")
    return v / norm


def tx(m):
    return np.swapaxes(m, -2, -1)


def apply(m, v):
    return (m @ v[..., None])[..., 0]


def matrix(q):
    q = np.asarray(q, dtype=float)
    x, y, z, w = np.moveaxis(unit(q), -1, 0)
    return np.stack((1-2*(y*y+z*z), 2*(x*y-w*z), 2*(x*z+w*y),
                     2*(x*y+w*z), 1-2*(x*x+z*z), 2*(y*z-w*x),
                     2*(x*z-w*y), 2*(y*z+w*x), 1-2*(x*x+y*y)), axis=-1).reshape(q.shape[:-1]+(3,3))


def quats(m):
    """Pick the best conditioned component at each sample, including near pi."""
    diag = np.diagonal(m, axis1=-2, axis2=-1)
    trace = diag.sum(axis=-1)
    norms = np.column_stack((1 + 2*diag-trace[:,None], 1+trace))
    out = np.zeros((len(m),4))
    for i in range(4):
        sel = norms.argmax(axis=1) == i
        a = m[sel]
        base = np.sqrt(np.maximum(0, norms[sel,i]))/2
        out[sel,i] = base
        if i == 3:
            for k in range(3):
                j,h=(k+1)%3,(k+2)%3
                out[sel,k] = (a[:,h,j]-a[:,j,h])/(4*base)
        else:
            j,h=(i+1)%3,(i+2)%3
            out[sel,j]=(a[:,i,j]+a[:,j,i])/(4*base)
            out[sel,h]=(a[:,i,h]+a[:,h,i])/(4*base)
            out[sel,3]=(a[:,h,j]-a[:,j,h])/(4*base)
    signs=np.where((out[1:]*out[:-1]).sum(axis=1)<0,-1.,1.)
    out[1:] *= np.cumprod(signs)[:,None]
    return unit(out)


def swing(start, end):
    a,b = unit(start),unit(end)
    q = np.column_stack((np.cross(a,b), 1+(a*b).sum(axis=1)))
    opposite = np.linalg.norm(q,axis=1)<1e-6
    if np.any(opposite):
        vectors=a[opposite]
        alt=np.eye(3)[np.abs(vectors).argmin(axis=1)]
        q[opposite,:3]=unit(np.cross(vectors,alt))
        q[opposite,3]=0
    return matrix(q)


class Pose:
    def __init__(self, parents, read):
        self.parents, self.read = parents, read
        self.rotation, self.position = {}, {}
        self.overrides = {}

    def world(self, name):
        if name not in self.rotation:
            local, offset = self.read(name)
            parent = self.parents[name]
            if parent:
                parent_q, parent_p = self.world(parent)
                self.position[name] = parent_p + apply(parent_q, offset)
                self.rotation[name] = parent_q @ local
            else:
                self.rotation[name], self.position[name] = local, offset
            if name in self.overrides:
                self.rotation[name] = self.overrides[name]
        return self.rotation[name], self.position[name]

    def set_world(self, name, q, output):
        # Children are requested only after their ancestors have been placed.
        parent_q = self.world(self.parents[name])[0]
        self.world(name)
        self.rotation[name] = self.overrides[name] = q
        output[name] = quats(tx(parent_q) @ q)


class Donor:
    def __init__(self, data, ref):
        tracks=data["character"]["frames_by_bone"]
        self.names={n.lower():n for n in tracks}
        for side in SIDES:
            for digit in ("Thumb","Index","Middle","Ring","Pinky"):
                for segment in (1,2,3):
                    key=f"{side}{digit}{segment}"
                    if key in tracks:
                        self.names[f"{side}Hand{digit}{segment}".lower()]=key
        self.center=ref["center_frame"]
        self.phases=np.linspace(0,1,len(ref["frames"]))
        self.frame=matrix(ref["wheel_orientation_world_xyzw"])
        self.degrees=np.array(data["wheel_phase"]["degrees_by_frame"])
        self.points={k:np.array([row["xyz_radii"][k] for row in ref["frames"]])
                     for k in ref["frames"][0]["xyz_radii"]}
        parents={n:k["parent"] for n,k in data["skeleton"]["bones"].items()}
        def read(name):
            samples=np.asarray(tracks[name])
            return matrix(samples[:,3:7]), samples[:,:3]
        self.pose=Pose(parents,read)

