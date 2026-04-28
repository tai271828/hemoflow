# import os.path
# import sys
import numpy as np

import slice
import perimeter
# import multiprocessing
from util import padVoxelArray

from stl import mesh

def voxelize(inputFile, targetElements, isMeshAShell = False, domainData = None, rotation =-1,
             slice_vertex_epsilon=slice._SLICE_EPSILON):
    mesh = list(import_stl_file(inputFile))

    if domainData is None:
        scale, shift, domain, bounding_box = slice.calculateScaleAndShift(mesh, targetElements)
    else:
        scale, shift, domain, bounding_box = domainData

    mesh = list(slice.scaleAndShiftMesh(mesh, scale, shift))
    #Note: vol should be addressed with vol[z][x][y]
    
    # If it is a shell mesh, rotate here for different projections!
    if rotation == 0:
        mesh = list(slice.swapAxis(mesh, 0, 1))
        domain = [domain[1], domain[0], domain[2]]
    elif rotation == 1:
        mesh = list(slice.swapAxis(mesh, 1, 2))
        domain = [domain[0], domain[2], domain[1]]

    vol = np.zeros((domain[2],domain[0],domain[1]), dtype=bool)

    ## Parallel version
    # procs = []
    # manager = multiprocessing.Manager()
    # return_dict = manager.dict()
    # #pool = multiprocessing.Pool(multiprocessing.cpu_count())
    # pool = multiprocessing.Pool(2)
    # for height in range(int(domain[2])):
    #     p = multiprocessing.Process(
    #         target=render_slice,
    #         args=(mesh, height, domain, return_dict, isMeshAShell)
    #         )
    #     procs.append(p)
    #     p.start()

    # pool.close()

    # for p in procs:
    #     p.join()
    # d = return_dict
    
    ## Serial for debugging
    # Precompute the set of vertex Z values once on the scaled/shifted mesh
    # (and post-rotation if any), so render_slice can do an O(1) coincidence
    # check per slice instead of rescanning the mesh.
    vertex_zs = slice.collectVertexZs(mesh)

    d={}
    for height in range(int(domain[2])):
        render_slice(mesh, height, domain, d, isMeshAShell,
                     slice_vertex_epsilon=slice_vertex_epsilon, vertex_zs=vertex_zs)

    for key, value in d.items():
        vol[key] = value

    vol, domain = padVoxelArray(vol)

    # If it is a shell mesh, rotate back here the different projections!
    if rotation == 0:
        vol = np.swapaxes(vol, 1, 2)
    elif rotation == 1:
        #mesh = slice.swapAxisWithZ(mesh, 1)
        vol = np.swapaxes(vol, 0, 2)


    # At the very end here fix the [z][x][y] back to [x][y][z]
    vol = np.swapaxes(vol, 0, 2)
    vol = np.swapaxes(vol, 0, 1)

    return (vol, (scale, shift, domain, bounding_box))

def render_slice(mesh, height, domain, return_dict, isMeshAShell,
                 slice_vertex_epsilon=slice._SLICE_EPSILON, vertex_zs=None):
    # Never perturb the first or last Z-slice. For a closed vessel mesh those
    # slices are the end caps: an empty (un-perturbed) cap is correctly painted
    # as wall by createFluidSolid.createWalls, which is the desired semantics.
    # Perturbing would trace the cap perimeter and fill the disc as fluid,
    # opening the closed end of the domain. The phantom-wall bug we care about
    # only affects *interior* slices, so excluding boundaries costs nothing.
    if height == 0 or height == int(domain[2]) - 1:
        effective_vertex_zs = None
    else:
        effective_vertex_zs = vertex_zs
    lines = slice.toIntersectingLines(mesh, height,
                                      epsilon=slice_vertex_epsilon,
                                      vertex_zs=effective_vertex_zs)
    prepixel = np.zeros((domain[0], domain[1]), dtype=bool)
    perimeter.linesToVoxels(lines, prepixel, isMeshAShell)
    return_dict[height] = prepixel

def import_stl_file(inputFile):
    imported_mesh = mesh.Mesh.from_file(inputFile)
    return get_stl_faces(imported_mesh)

def get_stl_faces(mesh):
    for i, j, k in zip(mesh.v0, mesh.v1, mesh.v2):
        yield (tuple(i), tuple(j), tuple(k))


if __name__ == '__main__':
    #voxelize('Files/Mesh_30000_faces/Mesh_21000.stl', 'Files/meshmesh.nrrd', pow(189, 3))
    #voxelize('test2.stl', 'test2.nrrd', pow(200,3))

    import sys
    if len(sys.argv) < 4:
        print("Usage:", sys.argv[0], "input.nrrd output.nrrd targetElementNum")        
        sys.exit(-1)

    from nrrd import write

    outputVolume, domainData = voxelize(sys.argv[1], int(sys.argv[3])) 

    write(sys.argv[2], outputVolume)