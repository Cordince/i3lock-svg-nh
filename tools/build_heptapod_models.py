#!/usr/bin/env python3
"""Build authored heptapod torso and hand/forearm sculpts as triangulated OBJ assets.

Profiles follow the shoulder/neck proportions in Huante's Arrival turnarounds;
surface folds and limb proportions are checked against the Movieclips footage.
No reference image is used as a billboard or texture on these models.
"""
from pathlib import Path
import math

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'cinema/models'


def bump(x, center, width):
    return math.exp(-((x-center)/width)**2)


def profile(points, t):
    i = min(len(points)-2, int(t*(len(points)-1)))
    f = t*(len(points)-1)-i
    # Catmull-Rom keeps the sculpt continuous across authored cross sections.
    a, b, c, d = (points[max(0, min(len(points)-1, j))] for j in (i-1, i, i+1, i+2))
    return tuple(.5*((2*b[k])+(-a[k]+c[k])*f+(2*a[k]-5*b[k]+4*c[k]-d[k])*f*f+
                       (-a[k]+3*b[k]-3*c[k]+d[k])*f*f*f) for k in range(len(b)))


TORSO = [
    # height, half-width, half-depth, forward/backward sweep
    (0.0,.43,.38,0), (.30,.51,.43,.02), (.65,.66,.49,.04),
    (1.0,.86,.57,.04), (1.35,1.02,.65,.02), (1.7,1.12,.67,0),
    (2.0,1.10,.60,.03), (2.25,.94,.49,.06), (2.5,.65,.37,.09),
    (2.75,.40,.30,.10), (3.0,.30,.27,.08), (3.25,.29,.28,.03),
    (3.48,.24,.24,-.04), (3.65,.12,.14,-.10), (3.70,.002,.002,-.12),
]
PALM = [
    (-.46,.15,.10,0), (-.35,.29,.13,-.01), (-.18,.38,.18,-.02),
    (.04,.36,.205,0), (.24,.28,.17,.025), (.43,.19,.12,.04),
    (.66,.14,.13,.08), (.80,.12,.12,.10),
]
FOREARM = [
    (.58,.14,.13), (.82,.16,.15), (1.1,.23,.21), (1.45,.34,.27),
    (1.85,.40,.33), (2.25,.43,.37), (2.7,.47,.42), (3.3,.52,.46),
    (4.0,.57,.50), (4.8,.62,.56),
]


def torso(u, theta):
    y, rx, rz, sweep = profile(TORSO, u)
    co, si = math.cos(theta), math.sin(theta)
    x = rx*co
    front = max(0, -si)**5
    # Paired pectoral masses, a recessed sternum, and an abdominal tendon fan.
    chest = .13*bump(abs(x),.49,.38)*bump(y,1.92,.40)
    ribs = sum(.045*bump(y, h+.10*abs(x),.085)*bump(abs(x),.43,.43)
               for h in (.88,1.10,1.32,1.54))
    sternum = -.065*bump(x,0,.095)*bump(y,1.6,.9)
    lower = .033*math.cos(theta*7+.6*math.sin(y*2))*bump(y,.55,.72)
    shoulders = .024*math.sin(theta*12+y*11)*bump(y,2.2,.5)
    tendons = .013*math.cos(theta*27 + y*2.8)*bump(y,1.0,1.65)
    # Neck folds descend into the shoulder girdle; the cranium has no human face.
    neck = .025*math.cos(theta*9)*bump(y,2.8,.48)
    displacement = front*(chest+ribs+sternum)+lower+shoulders+tendons+neck
    z = sweep+(rz+displacement)*si
    x += co*(lower+tendons+shoulders)
    return (x,y,z)


HAND = PALM + [(y,rx,rz,.32*y) for y,rx,rz in FOREARM if y > 1.0]


def hand(u, theta):
    # One welded surface from palm to upper forearm; no overlapping primitive
    # seams at the wrist. The longitudinal flexors continue across the joint.
    y,rx,rz,sweep=profile(HAND,u)
    co,si=math.cos(theta),math.sin(theta)
    upper=max(0,min(1,(y-.3)/.9));upper=upper*upper*(3-2*upper)
    tendons=.018*math.cos(theta*7+y*.7)
    wrist=.009*math.sin(y*65+math.sin(theta*3))*bump(y,1.05,.42)
    wrinkle=.003*math.sin(y*94+math.sin(theta*7)*1.3)
    x=-.21*y*upper+(rx+tendons+wrist+wrinkle)*co
    z=sweep+(rz+tendons+wrist+wrinkle)*si
    if si<0:
        z+=.105*bump(x,0,.105)*bump(y,-.06,.115)*(-si)**8
    return (x,y,z)


def subtract(a,b): return tuple(x-y for x,y in zip(a,b))
def normal(surface,u,theta):
    du=subtract(surface(min(1,u+.0003),theta),surface(max(0,u-.0003),theta))
    dv=subtract(surface(u,theta+.0003),surface(u,theta-.0003))
    n=(du[1]*dv[2]-du[2]*dv[1],du[2]*dv[0]-du[0]*dv[2],du[0]*dv[1]-du[1]*dv[0])
    length=math.sqrt(sum(x*x for x in n)) or 1
    return tuple(x/length for x in n)


def write_mesh(name, surface, rings, sides):
    vertices=[];normals=[]
    for i in range(rings+1):
        for j in range(sides):
            u=i/rings;theta=j/sides*math.tau
            vertices.append(surface(u,theta));normals.append(normal(surface,u,theta))
    lines=['# Authored Arrival study sculpt; rebuild with tools/build_heptapod_models.py',f'o {name}']
    lines += ['v '+' '.join(f'{x:.6f}' for x in p) for p in vertices]
    lines += ['vn '+' '.join(f'{x:.6f}' for x in n) for n in normals]
    for i in range(rings):
        for j in range(sides):
            a=i*sides+j+1;b=(i+1)*sides+j+1;c=i*sides+(j+1)%sides+1;d=(i+1)*sides+(j+1)%sides+1
            for face in [(a,b,c),(c,b,d)]: lines.append('f '+' '.join(f'{k}//{k}' for k in face))
    # Close both ends. Triangles lie in the small terminal/attachment rings.
    for base in (0,rings*sides):
        for j in range(1,sides-1):
            face=(base+1,base+j+1,base+j+2)
            lines.append('f '+' '.join(f'{k}//{k}' for k in face))
    (OUT/f'{name}.obj').write_text('\n'.join(lines)+'\n')


if __name__ == '__main__':
    OUT.mkdir(parents=True,exist_ok=True)
    write_mesh('heptapod-torso',torso,128,80)
    write_mesh('heptapod-hand',hand,160,80)
