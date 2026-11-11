#!/usr/bin/env python3
"""Build authored heptapod torso and hand/forearm sculpts as triangulated OBJ assets.

Profiles follow the rounded crown, neck and shoulder proportions in the final
film close-ups, with Huante's turnarounds supplying obscured anatomy. Surface
folds and limb proportions are checked against Movieclips and Paramount footage.
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
    (0.0,.47,.40,0), (.30,.55,.46,.02), (.65,.67,.52,.04),
    (1.0,.84,.60,.04), (1.35,1.01,.68,.02), (1.7,1.15,.71,0),
    (2.0,1.13,.65,.03), (2.25,.99,.55,.06), (2.5,.76,.46,.10),
    (2.75,.58,.41,.13), (3.0,.53,.41,.13), (3.25,.54,.43,.09),
    (3.48,.52,.43,.02), (3.68,.46,.38,-.04), (3.84,.32,.28,-.08),
    (3.94,.16,.15,-.10), (3.98,.002,.002,-.10),
]
PALM = [
    (-.55,.025,.03,0), (-.46,.14,.10,0), (-.35,.30,.16,-.01), (-.18,.40,.21,-.02),
    (.04,.39,.23,0), (.24,.32,.20,.025), (.43,.22,.15,.04),
    (.66,.15,.14,.08), (.80,.145,.14,.10),
]
FOREARM = [
    (.58,.15,.14), (.82,.17,.16), (1.1,.23,.21), (1.45,.34,.29),
    (1.85,.43,.35), (2.25,.47,.39), (2.7,.49,.43), (3.3,.53,.47),
    (4.0,.58,.51), (4.8,.63,.56),
]


def torso(u, theta):
    y, rx, rz, sweep = profile(TORSO, u)
    co, si = math.cos(theta), math.sin(theta)
    x = rx*co
    front = max(0, -si)**5
    # Paired pectoral masses, a recessed sternum, and an abdominal tendon fan.
    chest = .10*bump(abs(x),.49,.38)*bump(y,1.92,.40)
    ribs = sum(.025*bump(y, h+.10*abs(x),.085)*bump(abs(x),.43,.43)
               for h in (.88,1.10,1.32,1.54))
    sternum = -.065*bump(x,0,.095)*bump(y,1.6,.9)
    lower = .033*math.cos(theta*7+.6*math.sin(y*2))*bump(y,.55,.72)
    shoulders = .016*math.sin(theta*12+y*11)*bump(y,2.2,.5)
    tendons = .005*math.cos(theta*27 + y*2.8+math.sin(theta*5+y))*bump(y,1.0,1.65)
    # Neck folds descend into the shoulder girdle; the cranium has no human face.
    neck = .012*math.cos(theta*9)*bump(y,2.8,.48)
    displacement = front*(chest+ribs+sternum)+lower+shoulders+tendons+neck
    z = sweep+(rz+displacement)*si
    x += co*(lower+tendons+shoulders)
    return (x,y,z)


HAND = PALM + [(y,rx,rz,.32*y) for y,rx,rz in FOREARM if y > 1.0]
ARM_PATH = [
    (-.46,0,0), (0,0,0), (.4,.025,.02), (.8,.085,.13),
    (1.2,.10,.28), (1.7,-.14,.49), (2.3,-.48,.80),
    (3.0,-.73,1.12), (3.8,-.93,1.45), (4.8,-1.08,1.80),
]


def centerline(y):
    for i,(a,b) in enumerate(zip(ARM_PATH,ARM_PATH[1:])):
        if y <= b[0]:
            t=max(0,min(1,(y-a[0])/(b[0]-a[0])))
            previous=ARM_PATH[max(0,i-1)];following=ARM_PATH[min(len(ARM_PATH)-1,i+2)]
            span=b[0]-a[0]
            return tuple((2*t**3-3*t*t+1)*a[k]+(t**3-2*t*t+t)*span*(b[k]-previous[k])/(b[0]-previous[0])+
                         (-2*t**3+3*t*t)*b[k]+(t**3-t*t)*span*(following[k]-a[k])/(following[0]-a[0]) for k in (1,2))
    return ARM_PATH[-1][1:]


def hand(u, theta):
    # One welded surface from palm to upper forearm; no overlapping primitive
    # seams at the wrist. The longitudinal flexors continue across the joint.
    y,rx,rz,sweep=profile(HAND,u)
    co,si=math.cos(theta),math.sin(theta)
    center_x,center_z=centerline(y)
    # The writing close-ups show oblique carpal folds and long flexor cords;
    # continuous, evenly spaced circumferential bands read like a hose.
    tendons=.018*math.cos(theta*7+y*.7)*bump(y,.1,.65)
    tendons+=.010*math.cos(theta*5+math.sin(y*1.7))*bump(y,2.0,1.8)
    wrist=.002*math.sin(y*48+co*13+math.sin(y*5+theta*3)*2)*bump(y,1.05,.45)
    wrinkle=.0007*math.sin(y*71+math.sin(theta*11+y*4)*4)*bump(y,1.7,1.3)
    knuckle=.024*math.cos(theta*7)*bump(y,-.22,.16)
    x=center_x+(rx+tendons+wrist+wrinkle+knuckle)*co
    z=center_z+(rz+tendons+wrist+wrinkle+knuckle)*si
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
