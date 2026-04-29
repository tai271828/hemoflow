import math
from collections import defaultdict
import numpy as np

def linesToVoxels(lineList, pixels, isShell):
    if isShell:
        for x in range(len(pixels)):
            #isBlack = False
            lines = list(findRelevantLines(lineList, x))
            targetYs = list(map(lambda line:int(generateY(line,x)),lines))
            for y in range(len(pixels[x])):
                #if isBlack:
                #    pixels[x][y] = True
                if y in targetYs:
                    for line in lines:
                        if onLine(line, x, y):
                            #isBlack = not isBlack
                            pixels[x][y] = True
    else:
        for x in range(len(pixels)):
            isBlack = False
            lines = list(findRelevantLines(lineList, x))
            targetYs = list(map(lambda line:int(generateY(line,x)),lines))

            # SANITY CHECK: Execute BEFORE the loop to ensure we catch bounds errors
            import sys
            if getattr(sys.modules.get('__main__'), 'DEBUG_MODE', False) and targetYs:
                scanline_exit_wall = max(targetYs)
                scanline_exit_wall_value = max(max(line[0][1], line[1][1]) for line in lines)
                # The equal sign means the max scanline pixel index number would be larger than the max input domain pixel index number
                # since the pixel index starts at 0, the maximum pixel index is len(pixels[x]) - 1
                #
                # This sanity checks:
                # 1) if the generated scanline pixel number is more than the input domain pixel number
                # 2) if it is more, check max Y value of the generated scanlines ("scaneline_exit_wall_value").
                #
                # If scaneline_exit_wall_value is larger than len(pixels[x]),
                # then it is a clear sign that the input domain pixel number is too small to reach the outer wall of the geometry,
                # which means the geometry will be cut off and not fully captured in the voxelization.
                # This can make the following isBlack check not closed.
                if scanline_exit_wall >= len(pixels[x]):
                    print(
                        f"-> (DEBUG) SANITY CHECK FAILED:\n"
                        f"Domain total: {len(pixels[x])}, Expected: {scanline_exit_wall + 1} for {scanline_exit_wall_value}.\n\n"
                        f"The last voxel index along Y-axis of \n"
                        f"{x}-th voxel along X-axis in the voxel domain is {len(pixels[x]) - 1}-th (total {len(pixels[x])} pixels).\n"
                        f"However, the last voxel index of the voxel array of the scanline along Y-axis at x={x} z={lineList[0][0][2]} (xy-plane) is: {scanline_exit_wall}\n"
                        f"with value {scanline_exit_wall_value}.\n"
                        f"Total voxel number of the scanline should be {scanline_exit_wall} rather than {len(pixels[x])}."
                    )


            for y in range(len(pixels[x])):
                if isBlack:
                    pixels[x][y] = True
                if y in targetYs:
                    for line in lines:
                        if onLine(line, x, y):
                            isBlack = not isBlack
                            pixels[x][y] = True

            if isBlack:
                print("An error has occured at x%sz%s - is the geometry watertight?"%(x,lineList[0][0][2]))


# Voxelize solid, watertight body
def linesToVoxelsSolid(lineList, pixels):
    for x in range(len(pixels)):
        isBlack = False
        lines = list(findRelevantLines(lineList, x))
        targetYs = list(map(lambda line:int(generateY(line,x)),lines))
        for y in range(len(pixels[x])):
            if isBlack:
                pixels[x][y] = True
            if y in targetYs:
                for line in lines:
                    if onLine(line, x, y):
                        isBlack = not isBlack
                        pixels[x][y] = True

        if isBlack:
            print("An error has occured at x%sz%s - is the geometry watertight?"%(x,lineList[0][0][2]))


def linesToVoxelsShell(lineList, pixels):
    for x in range(len(pixels)):
        #isBlack = False
        lines = list(findRelevantLines(lineList, x))
        targetYs = list(map(lambda line:int(generateY(line,x)),lines))
        for y in range(len(pixels[x])):
            #if isBlack:
            #    pixels[x][y] = True
            if y in targetYs:
                for line in lines:
                    if onLine(line, x, y):
                        #isBlack = not isBlack
                        pixels[x][y] = True

# This only voxelises the outline. However, it is not watertight!
def linesToVoxelsShell2(lineList, pixels):
    for line in lineList:
        x1, y1 = line[0][0], line[0][1]
        x2, y2 = line[1][0], line[1][1]
        pixels[int(x1)][int(y1)] = True
        pixels[int(x2)][int(y2)] = True


def findRelevantLines(lineList, x, ind=0):
    for line in lineList:
        same = False
        above = False
        below = False
        for pt in line:
            if pt[ind] > x:
                above = True
            elif pt[ind] == x:
                same = True
            else:
                below = True
        if above and below:
            yield line
        elif same and above:
            yield line


def generateY(line, x):
    if line[1][0] == line[0][0]:
        return -1
    ratio = (x - line[0][0]) / (line[1][0] - line[0][0])
    ydist = line[1][1] - line[0][1]
    newy = line[0][1] + ratio * ydist
    return newy


def onLine(line, x, y):
    newy = generateY(line, x)
    if int(newy) != y:
        return False
    if int(line[0][0]) != x and int(line[1][0]) != x and (max(line[0][0], line[1][0]) < x or min(line[0][0], line[1][0]) > x):
        return False
    if int(line[0][1]) != y and int(line[1][1]) != y and (max(line[0][1], line[1][1]) < y or min(line[0][1], line[1][1]) > y):
        return False
    return True

