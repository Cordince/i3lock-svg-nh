#!/usr/bin/env python3
"""Bake continuous foreground sculpts; NumPy is needed only to rebuild assets.

Signed distances smoothly merge the wrist, palm and seven curled digits. The
runtime interpolates the folded/writing/contact sculpts and traces their actual
surface, avoiding separate caps, intersecting tubes and flat webbing panels.
"""
from pathlib import Path
import math
import struct
import numpy as np
from build_heptapod_models import HAND, profile, centerline

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'cinema/models'
SIZE = (128, 224, 128)
LOW = (-1.8, -1.5, -.75)
HIGH = (1.5, 5.1, 2.5)


def smooth_union(a, b, width):
    h = np.maximum(width-np.abs(a-b), 0)/width
    return np.minimum(a, b)-h*h*width*.25


def curve(a,b,c,d,t):
    return (1-t)**3*a+3*(1-t)**2*t*b+3*(1-t)*t*t*c+t**3*d


def bake(name, opening):
    nx,ny,nz=SIZE
    x=(LOW[0]+(np.arange(nx,dtype=np.float32)+.5)*(HIGH[0]-LOW[0])/nx)[None,None,:]
    y=(LOW[1]+(np.arange(ny,dtype=np.float32)+.5)*(HIGH[1]-LOW[1])/ny)[None,:,None]
    z=(LOW[2]+(np.arange(nz,dtype=np.float32)+.5)*(HIGH[2]-LOW[2])/nz)[:,None,None]
    samples=np.array([profile(HAND,t) for t in np.linspace(0,1,2048)])
    ys=y.ravel()
    rx=np.interp(ys,samples[:,0],samples[:,1])[None,:,None]
    rz=np.interp(ys,samples[:,0],samples[:,2])[None,:,None]
    centres=np.array([centerline(float(v)) for v in ys])
    cx=centres[:,0][None,:,None];cz=centres[:,1][None,:,None]
    # Elliptic loft with a rounded carpal joint and a shallow emission recess.
    distance=(np.sqrt(((x-cx)/rx)**2+((z-cz)/rz)**2)-1)*np.minimum(rx,rz)
    distance=np.maximum(distance,np.maximum(samples[0,0]-y,y-samples[-1,0]))
    distance=distance.astype(np.float32)
    for finger in range(7):
        angle=math.tau*finger/7+.16
        radial=np.array((math.cos(angle),math.sin(angle),0))
        spread=radial[0]*opening
        length=.88+.14*math.sin(finger*2.7)
        a=radial*.28+np.array((0,0,-.13))
        b=np.array((spread*.55,.18*(1-opening)+radial[1]*.48*opening,-.22))
        joint=np.array((spread*.94,(.46*(1-opening)+radial[1]*.94*opening)*length,-.21))
        c=joint+np.array((-spread*.1,-.08,-.04))
        end=np.array((spread*1.05,(.78*(1-opening)+radial[1]*1.12*opening)*length,-.02+.29*(1-opening)))
        # Continuous samples across the flexor joint, with fleshy rounded tips.
        points=[curve(a,b,c,joint,t) for t in np.linspace(0,1,9)]
        points += [curve(joint,joint+np.array((spread*.1,.12,-.07)),end+np.array((spread*.05,-.12,-.10)),end,t) for t in np.linspace(0,1,8)[1:]]
        radii=np.r_[np.linspace(.108,.050,9),np.linspace(.050,.016,8)[1:]]
        digit=np.full((nz,ny,nx),10,dtype=np.float32)
        for i,(p,q) in enumerate(zip(points,points[1:])):
            v=q-p
            u=np.clip(((x-p[0])*v[0]+(y-p[1])*v[1]+(z-p[2])*v[2])/np.dot(v,v),0,1)
            d=np.sqrt((x-p[0]-u*v[0])**2+(y-p[1]-u*v[1])**2+(z-p[2]-u*v[2])**2)
            d-=radii[i]+u*(radii[i+1]-radii[i])
            digit=smooth_union(digit,d,.023)
        # Smoothly merged roots form thick tissue between the fingers. They
        # remain part of the same surface as the palm, with no floating panels.
        distance=smooth_union(distance,digit,.065)
    # Fold grooves are shallow carved channels through the folded cuff, not
    # disconnected fingers or a separate membrane pasted over the surface.
    for centre in (-.095,0,.095):
        front=-.285-.045*np.sin((y+.1)*4)
        cutter=np.sqrt((x-centre-.025*np.sin(y*6+centre*11))**2+(z-front)**2)-.014
        cutter=np.maximum(cutter,np.maximum(-.12-y,y-.70))
        influence=(1-opening)*.009
        distance=-smooth_union(-distance,cutter,influence+.003)
    distance=np.clip(distance,-2,2).astype('<f2')
    assert np.isfinite(distance).all()
    assert all(float(face.min())>0 for face in (distance[0],distance[-1],distance[:,0],distance[:,-1],distance[:,:,0],distance[:,:,-1])), 'sculpt touches its storage boundary'
    path=OUT/f'foreground-{name}.sdf'
    header=struct.pack('<8s3I6f',b'HPSDF01\0',*SIZE,*LOW,*HIGH)
    path.write_bytes(header+distance.tobytes(order='C'))
    print(f'{path}: {SIZE}, {path.stat().st_size} bytes',flush=True)


if __name__=='__main__':
    OUT.mkdir(parents=True,exist_ok=True)
    for name,opening in [('folded',.10),('writing',.40),('contact',.98)]:
        bake(name,opening)
