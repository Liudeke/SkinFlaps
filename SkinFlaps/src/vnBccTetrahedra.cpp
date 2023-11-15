////////////////////////////////////////////////////////////////////////////
// File: vnBccTetrahedra.cpp
// Author: Court Cutting MD
// Date: 9/24/2022 revision of 7/1/2016 original
// Purpose: Basic virtual noded cubes class where cubes in space are not unique, but may be duplicated by the use of virtual nodes.
//     Full description of this concept is given in original work by Molino N,  Bao Z, and Fedkiw R: http://physbam.stanford.edu/~fedkiw/papers/stanford2004-01.pdf
////////////////////////////////////////////////////////////////////////////

#include <tuple>
#include <assert.h>
#include <algorithm>
#include <iterator>
#include <set>
#include <array>
#include <functional>
#include "Mat3x3f.h"
#include "boundingBox.h"
#include "materialTriangles.h"
#include "vnBccTetrahedra.h"

// set up inverses for quick barycentric coord computation for the 6 material coord orientations.  Could also be used to compute deformation gradients if start orientation needed.
// Usually need only one if svd makes the starting orientation irrelevant since all tets have exactly the same shape and size.

Mat3x3f vnBccTetrahedra::_barycentricInverses[6] = {
	{-0.5f, 0.5f, 0.5f, 0.5f, 0.0f, 0.0f, 0.0f, 0.5f, -0.5f},
	{0.0f, 0.5f, -0.5f, -0.5f, 0.5f, 0.5f, 0.5f, 0.0f, 0.0f},
	{0.5f, 0.0f, 0.0f, 0.0f, 0.5f, -0.5f, -0.5f, 0.5f, 0.5f},
	{0.5f, -0.5f, -0.5f, 0.5f, 0.0f, 0.0f, 0.0f, -0.5f, 0.5f},
	{0.0f, -0.5f, 0.5f, 0.5f, -0.5f, -0.5f, 0.5f, 0.0f, 0.0f},
	{0.5f, 0.0f, 0.0f, 0.0f, -0.5f, 0.5f, 0.5f, -0.5f, -0.5f} };

void vnBccTetrahedra::clear()
{
	_nodeGridLoci.clear();
	_tetNodes.clear();
	_tetCentroids.clear();
	_tetHash.clear();
	_vertexTets.clear();
	_barycentricWeights.clear();
}

void vnBccTetrahedra::centroidToNodeLoci(const bccTetCentroid& centroid, short (&gridLoci)[4][3]) {
	// fixed for multires
	int c1, c2, hc, size, levelUpBit;
	centroidHalfAxisSize(centroid, hc, size);
	levelUpBit = (size << 1);
	c1 = hc < 2 ? hc + 1 : 0;
	c2 = hc > 0 ? hc - 1 : 2;
	for (int j = 0; j < 4; ++j) {
		gridLoci[j][0] = centroid[0];
		gridLoci[j][1] = centroid[1];
		gridLoci[j][2] = centroid[2];
	}
	if ((centroid[hc] & levelUpBit) == (centroid[c2] & levelUpBit)) {  // 0-1 Cartesian axis below 2-3 
		gridLoci[0][hc] -= size;
		gridLoci[1][hc] -= size;
		gridLoci[2][hc] += size;
		gridLoci[3][hc] += size;
		gridLoci[2][c2] += levelUpBit;
		gridLoci[3][c2] -= levelUpBit;
	}
	else {
		gridLoci[0][hc] += size;
		gridLoci[1][hc] += size;
		gridLoci[2][hc] -= size;
		gridLoci[3][hc] -= size;
		gridLoci[2][c2] -= levelUpBit;
		gridLoci[3][c2] += levelUpBit;
	}
	gridLoci[0][c1] -= levelUpBit;
	gridLoci[1][c1] += levelUpBit;
	for (int j = 0; j < 4; ++j) {  // don't bit shift as possibly negative
		gridLoci[j][0] *= 0.5f;
		gridLoci[j][1] *= 0.5f;
		gridLoci[j][2] *= 0.5f;
	}
}

void vnBccTetrahedra::gridLocusToLowestTetCentroid(const Vec3f &gridLocus, bccTetCentroid &tetCentroid)
{
	short tc[3]; //  vMin, vNow;
	float dxyz[3];
	for (int i = 0; i < 3; ++i) {
		tc[i] = (short)std::floor(gridLocus[i]);
		dxyz[i] = gridLocus[i] - tc[i];
	}

	// get closest tet using centroids. Of course this works, but next version is faster.
/*	Vec3f cLoc[6], found, newTC;
	unitCubeCentroids(tc, cLoc);
	float d, dMin = FLT_MAX;
	for (int i = 0; i < 6; ++i) {
		d = (gridLocus - cLoc[i]).length2();
		if (d < dMin) {
			dMin = d;
			found = cLoc[i];
		}
	}
	assert(dMin <= 1.0f);  // furthest distance to still be inside bcc tet
	newTC = found * 2.0001f;
	tetCentroid = { (unsigned short)newTC[0], (unsigned short)newTC[1], (unsigned short)newTC[2] }; */

	// 4 different diagonal vectors in unit cubes give 4 different centroid patterns
	Vec3f newC, center = Vec3f(tc[0] + 0.5f, tc[1] + 0.5f, tc[2] + 0.5f);
	newC = center;
	bool split[3] = { (bool)(tc[0] & 1), (bool)(tc[1] & 1), (bool)(tc[2] & 1) };
	if (split[0] == split[1] && split[0] == split[2]) {
		if (dxyz[0] > dxyz[1] && dxyz[0] > dxyz[2]) {
			newC[0] += 0.5f;
			if (dxyz[1] > dxyz[2]) 
				newC[2] -= 0.5f;
			else
				newC[1] -= 0.5f;
		}
		else if (dxyz[1] > dxyz[0] && dxyz[1] > dxyz[2]) {
			newC[1] += 0.5f;
			if (dxyz[0] > dxyz[2])
				newC[2] -= 0.5f;
			else 
				newC[0] -= 0.5f;
		}
		else {
			newC[2] += 0.5f;
			if (dxyz[0] > dxyz[1])
				newC[1] -= 0.5f;
			else
				newC[0] -= 0.5f;
		}
	}
	else if (split[0] == split[1]) {
		dxyz[2] = 1.0f - dxyz[2];
		if (dxyz[0] > dxyz[1] && dxyz[0] > dxyz[2]) {
			newC[0] += 0.5f;
			if (dxyz[1] > dxyz[2])
				newC[2] += 0.5f;
			else
				newC[1] -= 0.5f;
		}
		else if (dxyz[1] > dxyz[0] && dxyz[1] > dxyz[2]) {
			newC[1] += 0.5f;
			if (dxyz[0] > dxyz[2])
				newC[2] += 0.5f;
			else
				newC[0] -= 0.5f;
		}
		else {
			newC[2] -= 0.5f;
			if (dxyz[0] > dxyz[1])
				newC[1] -= 0.5f;
			else
				newC[0] -= 0.5f;
		}
	}
	else if (split[0] == split[2]) {
		dxyz[1] = 1.0f - dxyz[1];
		if (dxyz[0] > dxyz[1] && dxyz[0] > dxyz[2]) {
			newC[0] += 0.5f;
			if (dxyz[1] > dxyz[2])
				newC[2] -= 0.5f;
			else
				newC[1] += 0.5f;
		}
		else if (dxyz[1] > dxyz[0] && dxyz[1] > dxyz[2]) {
			newC[1] -= 0.5f;
			if (dxyz[0] > dxyz[2]) 
				newC[2] -= 0.5f;
			else
				newC[0] -= 0.5f;
		}
		else {
			newC[2] += 0.5f;
			if (dxyz[0] > dxyz[1])
				newC[1] += 0.5f;
			else
				newC[0] -= 0.5f;
		}
	}
	else {
		assert(split[1] == split[2]);
		dxyz[0] = 1.0f - dxyz[0];
		if (dxyz[0] > dxyz[1] && dxyz[0] > dxyz[2]) {
			newC[0] -= 0.5f;
			if (dxyz[1] > dxyz[2])
				newC[2] -= 0.5f;
			else
				newC[1] -= 0.5f;
		}
		else if (dxyz[1] > dxyz[0] && dxyz[1] > dxyz[2]) {
			newC[1] += 0.5f;
			if (dxyz[0] > dxyz[2])
				newC[2] -= 0.5f;
			else
				newC[0] += 0.5f;
		}
		else {
			newC[2] += 0.5f;
			if (dxyz[0] > dxyz[1])
				newC[1] -= 0.5f;
			else
				newC[0] += 0.5f;
		}
	}
	newC *= 2.0001f;
	tetCentroid = { (unsigned short)newC[0], (unsigned short)newC[1], (unsigned short)newC[2] };
//  veracity test
//	if (newC != found) {
//		assert(dxyz[0] == dxyz[1] || dxyz[0] == dxyz[2] || dxyz[1] == dxyz[2]);
//		bccTetCentroid at, tn = { (unsigned short)newC[0], (unsigned short)newC[1], (unsigned short)newC[2] };
//		int i;
//		for (i = 0; i < 4; ++i) {
//			faceAdjacentTet(tetCentroid, i, at);
//			if (at == tetCentroid)
//				break;
//		}
//		if (i > 3)
//			int junk = 0;
//	}
}

void vnBccTetrahedra::barycentricWeightToGridLocus(const int tet, const Vec3f& barycentricWeight, Vec3f& gridLocus) {
	int* tn = _tetNodes[tet].data();
	gridLocus = Vec3f((const short(&)[3]) * _nodeGridLoci[tn[0]].data()) * (1.0f - barycentricWeight[0] - barycentricWeight[1] - barycentricWeight[2]);
	for (int i = 1; i < 4; ++i)
		gridLocus += Vec3f((const short(&)[3]) * _nodeGridLoci[tn[i]].data()) * barycentricWeight[i - 1];
}

void vnBccTetrahedra::barycentricWeightToGridLocus(const bccTetCentroid &tetCentroid, const Vec3f &barycentricWeight, Vec3f &gridLocus)
{
	short gridLoci[4][3];
	centroidToNodeLoci(tetCentroid, gridLoci);
	gridLocus = Vec3f((const short(&)[3])gridLoci[0]) * (1.0f - barycentricWeight.X - barycentricWeight.Y - barycentricWeight.Z);
	for (int i = 1; i < 4; ++i)
		gridLocus += Vec3f((const short(&)[3])gridLoci[i]) * barycentricWeight[i-1];
}

void vnBccTetrahedra::vertexGridLocus(const int vertex, Vec3f &gridLocus)  // always material coords
{
	int *tn = _tetNodes[_vertexTets[vertex]].data();
	float *bw = _barycentricWeights[vertex].xyz;
	gridLocus = Vec3f((const short(&)[3])*_nodeGridLoci[tn[0]].data()) * (1.0f - *bw - bw[1] - bw[2]);
	for (int i = 1; i < 4; ++i)
		gridLocus += Vec3f((const short(&)[3])*_nodeGridLoci[tn[i]].data()) * bw[i - 1];
}

void vnBccTetrahedra::vertexMaterialCoordinate(const int vertex, std::array<float, 3> &matCoord) {
	Vec3f gridLocus;
	vertexGridLocus(vertex, gridLocus);
	gridLocus *= (float)_unitSpacing;
	gridLocus += _minCorner;
	matCoord[0] = gridLocus.X;
	matCoord[1] = gridLocus.Y;
	matCoord[2] = gridLocus.Z;
}

void vnBccTetrahedra::gridLocusToBarycentricWeight(const Vec3f &gridLocus, const bccTetCentroid &tetCentroid, Vec3f &barycentricWeight)
{  // fixed for multires
	Vec3f B(gridLocus);
	// set barycentric coordinate within that tet
	int hc, baryInv, size;
	centroidHalfAxisSize(tetCentroid, hc, size);
	if (size > 1) {  // sizes > 1 are guaranteed to be present and unique.
		auto pr = _tetHash.equal_range(tetCentroid);
		assert(std::distance(pr.first, pr.second) == 1);
		auto& tn = _tetNodes[pr.first->second];
		Vec3f tV[4];
		for (int i = 0; i < 4; ++i)
			tV[i].set((short(&)[3]) * _nodeGridLoci[tn[i]].data());
		Mat3x3f M(tV[1] - tV[0], tV[2] - tV[0], tV[3] - tV[0]);
		barycentricWeight = M.Robust_Solve_Linear_System(gridLocus - tV[0]);
		return;
	}
	std::array<short, 3> xyz;
	for (int i = 0; i < 3; ++i)
		xyz[i] = tetCentroid[i] >> 1;
	if ((xyz[hc] + xyz[(hc + 1) % 3]) & 1) {  // main axis below secondary
		if (hc < 1)
			baryInv = 0;
		else if (hc < 2)
			baryInv = 1;
		else
			baryInv = 2;
		// subtract grid locus of first point of tet
		B -= Vec3f((const short (&)[3]) * xyz.data());
		B[(hc + 1) % 3] += 1.0f;
	}
	else {
		if (hc < 1)
			baryInv = 3;
		else if (hc < 2)
			baryInv = 4;
		else
			baryInv = 5;
		B -= Vec3f((const short(&)[3]) *xyz.data());
		B[hc] -= 1.0f;
		B[(hc + 1) % 3] += 1.0f;
	}
	barycentricWeight = _barycentricInverses[baryInv] * B;
}

int vnBccTetrahedra::faceAdjacentMicrotet(const bccTetCentroid tc, const int face, bccTetCentroid&tcAdj)
{  // fundamental code for all topological path routines
	// triangle faces are listed cyclic from the 4 tet nodes. Face 0 and 2 are CW, 1 & 3 CCW.
	// Returns face # of the adjacent tet.  If adjacent centroid would be outside positive octant (illegal centroid), face return is -1.
#ifdef _DEBUG
	if ((tc[0] & 1) == 0 && (tc[1] & 1) == 0 && (tc[2] & 1) == 0)
		throw(std::logic_error("vnBccTetrahedra::faceAdjacentMicrotet() called with a macrotet argument."));
#endif
	int adjFace;
	tcAdj = tc;
	int aha, ha = tc[0] & 1 ? 0 : (tc[1] & 1 ? 1 : 2);
	aha = (ha + 1) % 3;
	if (((tc[ha] + tc[aha]) >> 1) & 1) {  // up tet
		if (face < 1 || face > 2) {
			adjFace = 1;
			if (tcAdj[ha] < 1)
				return -1;  // next line would be below bounds tet
			--tcAdj[ha];
			aha = (ha + 2) % 3;
			if (face > 2 && tcAdj[aha] < 1)
				return -1;  // next line would be below bounds tet
			tcAdj[aha] += face < 1 ? 1 : -1;
		}
		else {
			adjFace = face < 2 ? 3 : 0;
			++tcAdj[ha];
			if (face > 1 && tcAdj[aha] < 1)
				return -1;  // next line would be below bounds tet
			tcAdj[aha] += face < 2 ? 1 : -1;
		}
	}
	else {  // down tet
		if (face < 1 || face > 2) {
			adjFace = 2;
			++tcAdj[ha];
			aha = (ha + 2) % 3;
			if (face < 1 && tcAdj[aha] < 1)
				return -1;  // next line would be below bounds tet
			tcAdj[aha] += face < 1 ? -1 : 1;
		}
		else {
			adjFace = face < 2 ? 0 : 3;
			if (tcAdj[ha] < 1)
				return -1;  // next line would be below bounds tet
			--tcAdj[ha];
			if (face > 1 && tcAdj[aha] < 1)
				return -1;  // next line would be below bounds tet
			tcAdj[aha] += face < 2 ? 1 : -1;
		}
	}
	return adjFace;
}

void vnBccTetrahedra::nodeMicroCentroids(std::array<short, 3>& node, bccTetCentroid cntrd[24]) {
	int count = 0;
	for (int dim = 0; dim < 3; ++dim) {
		for (int pos = -1; pos < 2; pos += 2) {
			bccTetCentroid tc2, tc = {(unsigned short)node[0], (unsigned short)node[1], (unsigned short)node[2]};
			tc[dim] += pos;
			for (int i = 0; i < 3; ++i)
				tc[i] <<= 1;
			for (int i = 0; i < 4; ++i) {
				tc2 = tc;
				int hc = ((i >> 1) + 1 + dim) % 3;
				if (i & 1)
					++tc2[hc];
				else
					--tc2[hc];
				cntrd[count++] = tc2;
			}
		}
	}
}

void vnBccTetrahedra::unitCubeCentroids(const short(&minimumCorner)[3], bccTetCentroid(&cntrd)[6]) {
	// 4 different diagonal vectors in unit cubes give 4 differentcentroid patterns
	Vec3f cl[6];
	unitCubeCentroids(minimumCorner, cl);
	for (int i = 0; i < 6; ++i) {
		cl[i] *= 2.00001f;
		cntrd[i] = { (unsigned short)cl[i][0], (unsigned short)cl[i][1], (unsigned short)cl[i][2] };
	}
}

void vnBccTetrahedra::unitCubeCentroids(const short(&minimumCorner)[3], Vec3f(&centroidLoci)[6]) {
	// 4 different diagonal vectors in unit cubes give 4 differentcentroid patterns
	bool split[3] = { (bool)(minimumCorner[0] & 1), (bool)(minimumCorner[1] & 1), (bool)(minimumCorner[2] & 1)};
	if (split[0] == split[1] && split[0] == split[2]) {
		split[0] = true;  split[1] = true; split[2] = true;
	}
	else if(split[0] == split[1]){
		split[0] = false;  split[1] = false; split[2] = true;
	}
	else if (split[0] == split[2]) {
		split[0] = false;  split[1] = true; split[2] = false;
	}
	else{
		assert(split[1] == split[2]);
		split[0] = true;  split[1] = false; split[2] = false;
	}
	Vec3f center = Vec3f(minimumCorner[0] + 0.5f, minimumCorner[1] + 0.5f, minimumCorner[2] + 0.5f);
	for (int i = 0; i < 3; ++i) {
		int c2, c1 = i < 2 ? i + 1 : 0;
		c2 = c1 < 2 ? c1 + 1 : 0;
		for (int j = 0; j < 2; ++j) {
			Vec3f& c = centroidLoci[(i << 1) + j];
			c = center;
			if (j) {
				c[c1] -= 0.5f;
				c[c2] += split[i] ? 0.5f : -0.5f;
			}
			else {
				c[c1] += 0.5f;
				c[c2] += split[i] ? -0.5f : 0.5f;
			}
		}
	}
}

int vnBccTetrahedra::faceAdjacentMicrotets(const int tet, const int face, std::list<int> &adjTets)
{  // returns adjacent face index 0-3
	adjTets.clear();
	bccTetCentroid tcAdj;
	int adjFace = faceAdjacentMicrotet(_tetCentroids[tet], face, tcAdj);
	const int *tn = tetNodes(tet);
	std::set<int> faceNodes;
	for (int i = 0; i < 3; ++i)
		faceNodes.insert(tn[(face + i) & 3]);
	auto tr = _tetHash.equal_range(tcAdj);
	while (tr.first != tr.second){
		tn = tetNodes(tr.first->second);
		int i;
		for (i = 0; i < 3; ++i){
			if(faceNodes.find(tn[(adjFace + i) & 3]) == faceNodes.end())
				break;
		}
		if (i > 2)
			adjTets.push_back(tr.first->second);
		++tr.first;
	}
	return adjFace;
}

void vnBccTetrahedra::edgeNodes(const int tet, const int edge, int &n0, int &n1)
{ // input one of six edges in permutation order 0-123, 1-23, and 2-3
	int *tn = _tetNodes[tet].data();
	if (edge < 3) {
		n0 = tn[0];
		n1 = tn[edge + 1];
	}
	else if (edge < 5) {
		n0 = tn[1];
		n1 = tn[edge - 1];
	}
	else {
		n0 = tn[2];
		n1 = tn[3];
	}
}

void vnBccTetrahedra::edgeAdjacentMicrotets(const int tet, const int edge, std::list<int> &adjTets)
{ // input one of six edges in permutation order 0-123, 1-23, and 2-3
	throw(std::logic_error("Rewrite edgeAdjacentMicrotets()."));  // rewrite

/*	adjTets.clear();
	int n0, n1, *tn = _tetNodes[tet].data();
	edgeNodes(tet, edge, n0, n1);
	bccTetCentroid tc = _tetCentroids[tet];
	bool tetUp = (tc.xyz[(tc.halfCoordAxis + 1) % 3] + tc.xyz[tc.halfCoordAxis]) & 1;
	std::list<bccTetCentroid> nTets1;
	if (edge < 1){  // Cartesian edge
		if (!tetUp)
			++tc.xyz[tc.halfCoordAxis];
		for (int i = 0; i < 4; ++i){
			nTets1.push_back(tc);
			if (i & 1)
				nTets1.back().halfCoordAxis = (tc.halfCoordAxis + 2) % 3;
			if (i > 1)
				--nTets1.back().xyz[nTets1.back().halfCoordAxis];
		}
	}
	else if (edge > 4){  // edge==5 Cartesian edge
		// set midpoint of edge
		if (tetUp)
			++tc.xyz[tc.halfCoordAxis];
		for (int i = 0; i < 4; ++i){
			nTets1.push_back(tc);
			if (i > 1)
				nTets1.back().halfCoordAxis = (tc.halfCoordAxis + 1) % 3;
			if (i & 1)
				--nTets1.back().xyz[nTets1.back().halfCoordAxis];
		}
	}
	else{
		//  Get 6 diagonal centroid neighbors
		int c2, c1, c0 = tc.halfCoordAxis;
		c1 = c0 < 2 ? c0 + 1 : 0;
		c2 = c1 < 2 ? c1 + 1 : 0;
		for (int i = 0; i < 6; ++i){
			nTets1.push_back(tc);
			nTets1.back().halfCoordAxis = (c0 + (i>>1)) % 3;
			if (edge < 2){
				if (tetUp){
					if (i < 1){
						++nTets1.back().xyz[c2];
						--nTets1.back().xyz[c1];
					}
					else if (i>1 && i < 4){
						--nTets1.back().xyz[c1];
						i & 1 ? ++nTets1.back().xyz[c2] : ++nTets1.back().xyz[c0];
					}
					else if (i > 4){
						--nTets1.back().xyz[c1];
						++nTets1.back().xyz[c0];
					}
					else;
				}
				else{
					if (i < 1){
						--nTets1.back().xyz[c2];
						--nTets1.back().xyz[c1];
					}
					else if (i>1 && i < 4){
						--nTets1.back().xyz[c1];
						if (i & 1){
							++nTets1.back().xyz[c0];
							--nTets1.back().xyz[c2];
						}
					}
					else if(i>3){
						--nTets1.back().xyz[c2];
						i & 1 ? --nTets1.back().xyz[c1] : ++nTets1.back().xyz[c0];
					}
					else;
				}
			}
			else if (edge < 3){
				if (tetUp){
					if (i < 1){
						--nTets1.back().xyz[c1];
						--nTets1.back().xyz[c2];
					}
					else if (i>1 && i < 4){
						--nTets1.back().xyz[c1];
						i & 1 ? --nTets1.back().xyz[c2] : ++nTets1.back().xyz[c0];
					}
					else if (i > 3){
						--nTets1.back().xyz[c2];
						if (i & 1){
							--nTets1.back().xyz[c1];
							++nTets1.back().xyz[c0];
						}
					}
					else;
				}
				else{
					if (i < 1){
						++nTets1.back().xyz[c2];
						--nTets1.back().xyz[c1];
					}
					else if (i>1 && i < 4){
						--nTets1.back().xyz[c1];
						if (i & 1){
							++nTets1.back().xyz[c0];
							++nTets1.back().xyz[c2];
						}
					}
					else if (i>3){
						i & 1 ? --nTets1.back().xyz[c1] : ++nTets1.back().xyz[c0];
					}
					else;
				}
			}
			else if (edge < 4){
				if (tetUp){
					if (i < 1){
						++nTets1.back().xyz[c1];
						++nTets1.back().xyz[c2];
					}
					else if (i>1 && i < 4){
						i & 1 ? ++nTets1.back().xyz[c2] : ++nTets1.back().xyz[c0];
					}
					else if (i > 4){
						++nTets1.back().xyz[c1];
						++nTets1.back().xyz[c0];
					}
					else;
				}
				else{
					if (i < 1){
						++nTets1.back().xyz[c1];
						--nTets1.back().xyz[c2];
					}
					else if (i == 2){
						--nTets1.back().xyz[c2];
						++nTets1.back().xyz[c0];
					}
					else if (i>3){
						--nTets1.back().xyz[c2];
						i & 1 ? ++nTets1.back().xyz[c0] : ++nTets1.back().xyz[c1];
					}
					else;
				}
			}
			else{
				if (tetUp){
					if (i < 1){
						++nTets1.back().xyz[c1];
						--nTets1.back().xyz[c2];
					}
					else if (i>1 && i < 4){
						i & 1 ? --nTets1.back().xyz[c2] : ++nTets1.back().xyz[c0];
					}
					else if (i > 3){
						--nTets1.back().xyz[c2];
						if (i & 1){
							++nTets1.back().xyz[c0];
							++nTets1.back().xyz[c1];
						}
					}
					else;
				}
				else{
					if (i < 1){
						++nTets1.back().xyz[c1];
						++nTets1.back().xyz[c2];
					}
					else if (i == 2){
						++nTets1.back().xyz[c0];
						++nTets1.back().xyz[c2];
					}
					else if (i > 3){
						i & 1 ? ++nTets1.back().xyz[c1] : ++nTets1.back().xyz[c0];
					}
					else;
				}

			}
		}
	}
	for (auto &nt : nTets1){
		auto er = _tetHash.equal_range(nt.ll);
		while (er.first != er.second){
			tn = _tetNodes[er.first->second].data();
			int count = 0;
			for (int k = 0; k < 4; ++k){
				if (tn[k] == n0 || tn[k] == n1){
					++count;
					if (count > 1){
						adjTets.push_back(er.first->second);
						break;
					}
				}
			}
			++er.first;
		}
	} */
}

/* int vnBccTetrahedra::vertexConnectedToGridLocus(const int vertex, const Vec3f& locus)  // returns tet number at connected locus, -1 if no connection
{  // true if a straight line solid path between vertex and locus exists.

	throw(std::logic_error("Write vnBccTetrahedra::vertexConnectedToGridLocus()"));

	int vtet = _vertexTets[vertex];
	bccTetCentroid itc, vtc = _tetCentroids[vtet];
	gridLocusToLowestTetCentroid(locus, itc);
	Vec3f dv, vloc, iloc = locus *2.0f;
	vertexGridLocus(vertex, vloc);
	vloc *= 2.0f;
	// work in tc (x2) space

	assert(false);  // COURT finish writing me

	dv = iloc - vloc;
	float t, minT = FLT_MAX;
	for (int i = 0; i < 3; ++i) {
		t = (vtc[i] - vloc[i])/dv[i];
		if (t < minT)
			minT = t;
	}
	return false;
} */

int vnBccTetrahedra::vertexSolidLinePath(const int vertex, const Vec3f materialTarget) {  // if linear path through solid found, returns tet id containing materialTarget.
	//  Else if no path, return -1. VN y junction hit making search impossible returns -2.
	// remember that only microtets can virtual node and be duplicated.  This facilitates search through tets whose level is above 1.
	bccTetCentroid tc;
	gridLocusToLowestTetCentroid(materialTarget, tc);
	auto targetTets = _tetHash.equal_range(tc);
	int targetSize = 1;
	while (targetTets.first == targetTets.second) {
		tc = centroidUpOneLevel(tc);
		++targetSize;
		if (targetSize > _tetSubdivisionLevels)
			throw(std::logic_error("Algorithm error in vnBccTetrahedra::vertexSolidLinePath()\n"));
	}
	if (std::distance(targetTets.first, targetTets.second) == 1)
		return targetTets.first->second;
	// targetTets must be of size 1, multiple with same centroid. Unfortunately could be at virtual noded tet split sharing common nodes.
	assert(targetSize == 1);
	Vec3f vLoc, N;
	vertexGridLocus(vertex, vLoc);
	N = materialTarget - vLoc;
	int sizeNow, tetNow = _vertexTets[vertex];
	float p = 0.0f;
	auto& tn = _tetNodes[tetNow];
	int face, adjFace;
	std::set<int> nodeSet(tn.begin(), tn.end());
	auto tetIntersect = [&](const int &tet, int &tetSize, int& tetFace, float& lineParam) ->bool {
		short gl[4][3];
		centroidToNodeLoci(_tetCentroids[tet], gl);
		for (int i = 0; i < 3; ++i)
			if (gl[0][i] != gl[1][i]) {
				tetSize = ((gl[1][i] - gl[0][i]) >> 1);
				break;
			}
		for (tetFace = 0; tetFace < 4; ++tetFace) {
			Vec3f V0(gl[tetFace]), V1(gl[(tetFace + 1) & 3]), V2(gl[(tetFace + 2) & 3]);
			V1 -= V0;
			V2 -= V0;
			Mat3x3f M(N, V1, V2);
			Vec3f R = M.Robust_Solve_Linear_System(vLoc - V0);
			if (R[0] > p && R[1] >= 0.0f && R[1] <= 1.0f && R[2] >= 0.0f && R[2] <= 1.0f && R[1] + R[2] <= 1.0f) {
				lineParam = R[0];
				return true;
			}
		}
		return false;
	};
	if (!tetIntersect(tetNow, sizeNow, face, p))  // this is an error in the cutter upstream, not here.
		return -1;
	while(p < 1.0f){
		bccTetCentroid tcAdj;
		adjFace = faceAdjacentCentroid(_tetCentroids[tetNow], face, tcAdj);
		auto pr = _tetHash.equal_range(tcAdj);
		if (pr.first == pr.second) {
			bccTetCentroid tc = tcAdj;
			int count = 1;
			auto tit = _tetHash.end();
			do {
				tc = centroidUpOneLevel(tc);
				tit = _tetHash.find(tc);  // looking up will only find unique tets 
				++count;
			} while (count <= _tetSubdivisionLevels && tit == _tetHash.end());
			if (tit != _tetHash.end())
				tetNow = tit->second;
			else {
				// Now look down
				bccTetCentroid subC8[8];
				subtetCentroids(tcAdj, subC8);
				// center core hidden by corner tets, so only need first 4
			}
		}
		if (std::distance(pr.first, pr.second) == 1) {
			tetNow = pr.first->second;
		}
		while (pr.first != pr.second) {  // this must be a vrtual noded multi tet of size 1.
			auto& tn2 = _tetNodes[pr.first->second];
			if (sizeNow > 3)  // COURT this is oversimplified.  If preceeding neighbor is a large macrotet may need to use T junctions to find node connection if this simple test fails.
				assert(false);  // COURT write me
			int i;
			for (i = 0; i < 4; ++i) {  
				if (nodeSet.find(tn2[i]) != nodeSet.end()) {
					break;
				}
			}
			if (i < 4)
				break;
			++pr.first;
		}
		if (pr.first == pr.second)
			return -1;
		tetNow = pr.first->second;
		if (!tetIntersect(tetNow, sizeNow, face, p))  // this is an error in the cutter upstream, not here.
			return -1;
	}
	if (p > 1.0f)
		return tetNow;
	return -1;
}

/* bool vnBccTetrahedra::decreasingCentroidPath(const int startTet, const int targetTet, std::list<int>& tetPath)
{  // true if constantly decreasing distance centroid path exists. Rewritten for multires.
	Vec3f loc, target, dir;
	bccTetCentroid &tc = _tetCentroids[targetTet];
	target.set((float)tc[0], (float)tc[1], (float)tc[2]);
	target *= 0.5f;
	int tetNow = startTet;
	tc = _tetCentroids[startTet];
	loc.set((float)tc[0], (float)tc[1], (float)tc[2]);
	loc *= 0.5f;
	dir = target - loc;
	float d2, d2min = dir.length2();
	bool moved;
	tetPath.clear();
	struct branch{
		int lastTetTried;
		std::list<int> alternateTets;
	};
	int bestAdjFace = -1;
	std::list<branch> branches;
	while (tetNow != targetTet){
		std::list<int> adjTets, bestAdjTets;
		moved = false;
		for (int i = 0; i < 4; ++i){
			bccTetCentroid tcAdj;
			faceAdjacentCentroid(_tetCentroids[tetNow],i, tcAdj);
			Vec3f Vadj((const unsigned short (&)[3])*tcAdj.data());
			Vadj *= 0.5f;
			if ((Vadj - loc) * dir < 0.0f)
				continue;
			auto pr = _tetHash.equal_range(tcAdj);
			if (pr.first != pr.second) {

			}



			int adjFace = faceAdjacentMicrotets(tetNow, i, adjTets);
			if (i == bestAdjFace || adjTets.empty())
				continue;
			tc = _tetCentroids[adjTets.front()];
			loc.set((float)tc[0], (float)tc[1], (float)tc[2]);
			loc *= 0.5f;
			if ((d2 = (loc - target).length2()) < d2min){
				if (d2 == 0.0f) {
					for (auto at : adjTets) {
						if (at == targetTet)
							return true;
					}
					return false;
				}
				d2min = d2;
				tetNow = adjTets.front();
				bestAdjTets = std::move(adjTets);
				bestAdjFace = adjFace;
				moved = true;
			}
		}
		if (moved){
			if (bestAdjTets.size() > 1){
				branches.push_back(branch());
				branches.back().lastTetTried = tetNow;
				bestAdjTets.pop_front();
				branches.back().alternateTets = std::move(bestAdjTets);
			}
			tetPath.push_back(tetNow);
		}
		else{
			// try moving up branch list
			if (branches.empty())
				return false;
			while (tetPath.back() != branches.back().lastTetTried)  // COURT - not debugged yet
				tetPath.pop_back();
			tetNow = branches.back().alternateTets.front();
			tetPath.back() = tetNow;
			branches.back().alternateTets.pop_front();
			if (branches.back().alternateTets.empty())
				branches.pop_back();
		}
	}
	return true;
} */

void vnBccTetrahedra::materialCoordsToNodeSpatialVector()
{
	if (_nodeSpatialCoords == nullptr) {
		throw(std::logic_error("Trying to fill spatialCoordinateVector before it has been allocated and assigned."));
		exit(0);
	}
	for (int n = _nodeGridLoci.size(), i = 0; i < n; ++i) {
		const short* np = _nodeGridLoci[i].data();
		auto &vp = _nodeSpatialCoords[i];
		vp.set((float)np[0], (float)np[1], (float)np[2]);
		vp *= (float)_unitSpacing;
		vp += _minCorner;
	}
}

bool vnBccTetrahedra::insideTet(const bccTetCentroid& tc, const Vec3f& gridLocus) {
	int dd = 1, hc = -1;
	while (true) {
		if (tc[0] & dd) {
			hc = 0;
			break;
		}
		if (tc[1] & dd) {
			hc = 1;
			break;
		}
		if (tc[2] & dd) {
			hc = 2;
			break;
		}
		dd <<= 1;
	}
	dd <<= 1;
	int c1 = (hc + 1) % 3, c2 = (hc + 2) % 3;
	bool up = (tc[hc] & dd) == (tc[c2] & dd) ? true : false;
	if (up) {
		int V[2];
		V[0] = tc[c2] >> 1;
		if (dd < 3)
			V[1] = (tc[hc] >> 1);
		else
			V[1] = (tc[hc] >> 1) - (dd >> 2);
		if (gridLocus[c2] - gridLocus[hc] > V[0] - V[1])
			return false;
		if (-gridLocus[c2] - gridLocus[hc] > -V[0] - V[1])
			return false;
		if (dd < 3)
			++V[1];
		else
			V[1] += (dd >> 1);
		V[0] = tc[c1] >> 1;
		if (gridLocus[c1] + gridLocus[hc] > V[0] + V[1])
			return false;
		if (-gridLocus[c1] + gridLocus[hc] > -V[0] + V[1])
			return false;
	}
	else {
		int V[2];
		V[0] = tc[c2] >> 1;
		if (dd < 3)
			V[1] = (tc[hc] >> 1) + 1;
		else
			V[1] = (tc[hc] >> 1) + (dd >> 2);
		if (gridLocus[c2] + gridLocus[hc] > V[0] + V[1])
			return false;
		if (-gridLocus[c2] + gridLocus[hc] > -V[0] + V[1])
			return false;
		if (dd < 3)
			--V[1];
		else
			V[1] -= (dd >> 1);
		V[0] = tc[c1] >> 1;
		if (gridLocus[c1] - gridLocus[hc] > V[0] - V[1])
			return false;
		if (-gridLocus[c1] - gridLocus[hc] > -V[0] - V[1])
			return false;
	}
	return true;
}

bool vnBccTetrahedra::insideTet(const bccTetCentroid& tc, const std::array<short, 3>& nodeLocus) {
	int dd = 1, hc = -1;
	while (true) {
		if (tc[0] & dd) {
			hc = 0;
			break;
		}
		if (tc[1] & dd) {
			hc = 1;
			break;
		}
		if (tc[2] & dd) {
			hc = 2;
			break;
		}
		dd <<= 1;
	}
	dd <<= 1;
	int c1 = (hc + 1) % 3, c2 = (hc + 2) % 3;
	bool up = (tc[hc] & dd) == (tc[c2] & dd) ? true : false;
	if (up) {
		int V[2];
		V[0] = tc[c2] >> 1;
		if (dd < 3)
			V[1] = (tc[hc] >> 1);
		else
			V[1] = (tc[hc] >> 1) + (dd >> 2);
		if (nodeLocus[c2] - nodeLocus[hc] > V[0] - V[1])
			return false;
		if (-nodeLocus[c2] - nodeLocus[hc] > -V[0] - V[1])
			return false;
		if (dd < 3)
			++V[1];
		else
			V[1] += (dd >> 1);
		V[0] = tc[c1] >> 1;
		if (nodeLocus[c1] + nodeLocus[hc] > V[0] + V[1])
			return false;
		if (-nodeLocus[c1] + nodeLocus[hc] > -V[0] + V[1])
			return false;
	}
	else {
		int V[2];
		V[0] = tc[c2] >> 1;
		if (dd < 3)
			V[1] = (tc[hc] >> 1) + 1;
		else
			V[1] = (tc[hc] >> 1) + (dd >> 2);
		if (nodeLocus[c2] + nodeLocus[hc] > V[0] + V[1])
			return false;
		if (-nodeLocus[c2] + nodeLocus[hc] > -V[0] + V[1])
			return false;
		if (dd < 3)
			--V[1];
		else
			V[1] -= (dd >> 1);
		V[0] = tc[c1] >> 1;
		if (nodeLocus[c1] - nodeLocus[hc] > V[0] - V[1])
			return false;
		if (-nodeLocus[c1] - nodeLocus[hc] > -V[0] - V[1])
			return false;
	}
	return true;
}

int vnBccTetrahedra::parametricEdgeTet(const int vertex0, const int vertex1, const float param, Vec3f& gridLocus)
{  // new multi resolution version
	Vec3f tV[2];
	vertexGridLocus(vertex0, tV[0]);
	vertexGridLocus(vertex1, tV[1]);
	gridLocus = tV[0] * (1.0f - param) + tV[1] * param;

	bccTetCentroid tC;
	gridLocusToLowestTetCentroid(gridLocus, tC);  // new version only return a valid tc from current mesh
	// if this lowest level centroid not found, promote til found.
	int level = 1;
	assert(!_tetHash.empty());
	auto pr = _tetHash.equal_range(tC);
	while (pr.first == pr.second) {  // not found.  Move up centroid hierarchy if possible.
		tC = centroidUpOneLevel(tC);
		++level;
		if (level > 16)  // nobody would decimate this much
			throw(std::logic_error("Surface point chosen not embedded in an existing tetrahedron.\n"));
		pr = _tetHash.equal_range(tC);
	}
	if (level > 1) {  // upper level tets guaranteed not to virtual node so are unique
		assert(std::distance(pr.first, pr.second) == 1);
		return pr.first->second;
	}
	int tetOut = getVertexTetrahedron(vertex0);
	if (tC == _tetCentroids[tetOut])
		return tetOut;
	tetOut = getVertexTetrahedron(vertex1);
	if (tC == _tetCentroids[tetOut])
		return tetOut;
	// find candidate cubes
	int nTets = std::distance(pr.first, pr.second);
	if (nTets < 1) {
		assert(false);
		return -1;
	}
	else if (nTets < 2)
		return pr.first->second;
	else {
		std::list<int> tetPath;
		for (auto tcit = pr.first; tcit != pr.second; ++tcit) {
			for (int i = 0; i < 3; ++i) {

				assert(false);  // COURT debug me

				//			if (decreasingCentroidPath(tcit->second, _vertexTets[vertices[i]], tetPath))  // possibly wrong due to multires
				//				return tcit->second;
			}
		}
	}
	assert(false);
	return -1;
}

int vnBccTetrahedra::parametricTriangleTet(const int triangle, const float (&uv)[2], Vec3f& gridLocus)
{  // new multi resolution version
	Vec3f tV[3];
	const int* tr = _mt->triangleVertices(triangle);
	for (int i = 0; i < 3; ++i)
		vertexGridLocus(tr[i], tV[i]);
	gridLocus = tV[0] * (1.0f - uv[0] - uv[1]) + tV[1] * uv[0] + tV[2] * uv[1];
	bccTetCentroid tC;
	gridLocusToLowestTetCentroid(gridLocus, tC);  // new version only return a valid tc from current mesh
	// if this lowest level centroid not found, promote til found.
	int level = 1;
	assert(!_tetHash.empty());
	auto pr = _tetHash.equal_range(tC);
	while (pr.first == pr.second) {  // not found.  Move up centroid hierarchy if possible.
		tC = centroidUpOneLevel(tC);
		++level;
		if (level > 16)  // nobody would decimate this much
			throw(std::logic_error("Surface point chosen not embedded in an existing tetrahedron.\n"));
		pr = _tetHash.equal_range(tC);
	}
	if (level > 1) {  // upper level tets guaranteed not to virtual node so are unique
		assert(std::distance(pr.first, pr.second) == 1);
		return pr.first->second;
	}
	for (int i = 0; i < 3; ++i) {
		int tetOut = _vertexTets[tr[i]];
		if (tC == _tetCentroids[tetOut])
			return tetOut;
	}
	// find candidate cubes
	int nTets = std::distance(pr.first, pr.second);
	if (nTets < 1) {
		assert(false);
		return -1;
	}
	else if (nTets < 2)
		return pr.first->second;
	else {
		// look for shared neighbor node first
		for (auto tcit = pr.first; tcit != pr.second; ++tcit) {
			std::set<int> lnodes(_tetNodes[tcit->second].begin(), _tetNodes[tcit->second].end());
			for (int j, i = 0; i < 3; ++i) {
				auto& vt = _tetNodes[_vertexTets[tr[i]]];
				for (j = 0; j < 4; ++j) {
					if (lnodes.find(vt[j]) != lnodes.end())
						break;
				}
				if (j < 4)
					return tcit->second;
			}
		}
		for (int j, i = 0; i < 3; ++i) {
			int tet = vertexSolidLinePath(tr[i], gridLocus);
			if (tet > -1)
				return tet;
		}
	}
	return -1;
}

void vnBccTetrahedra::subtetCentroids(const bccTetCentroid& macroCentroid, bccTetCentroid(&subCentroids)[8]) {
	int hc, level;
	centroidHalfAxisSize(macroCentroid, hc, level);
	if (level < 2)
		throw(std::logic_error("Trying to get subtets from a level 1 centroid.\n"));
	int levelUp = level << 1, levelDown = level >> 1;
	int c1 = (hc + 1) % 3, c2 = (hc + 2) % 3;

	bool up = (macroCentroid[hc] & levelUp) == (macroCentroid[c2] & levelUp) ? true : false;

	for (int i = 0; i < 8; ++i)
		subCentroids[i] = macroCentroid;
	auto markInvalid = [&](bccTetCentroid& tc) {
		for (int j = 0; j < 3; ++j)
			tc[j] = USHRT_MAX;
	};
	// 4 corners have same hc.  List corner tets in same order as nodes to ease later processing.
	if (up) {
		subCentroids[0][hc] -= levelDown;
		subCentroids[1][hc] -= levelDown;
		subCentroids[2][hc] += levelDown;
		subCentroids[3][hc] += levelDown;
	}
	else {
		subCentroids[0][hc] += levelDown;
		subCentroids[1][hc] += levelDown;
		subCentroids[2][hc] -= levelDown;
		subCentroids[3][hc] -= levelDown;
	}
	if (subCentroids[0][c1] < level)
		markInvalid(subCentroids[0]);
	else
		subCentroids[0][c1] -= level;
	subCentroids[1][c1] += level;
	if(subCentroids[up ? 3 : 2][c2] < level)
		markInvalid(subCentroids[up ? 3 : 2]);
	else
		subCentroids[up ? 3 : 2][c2] -= level;
	subCentroids[up ? 2 : 3][c2] += level;
	// 4 core tets ring around hc axis
	if(subCentroids[4][c1] < levelDown)
		markInvalid(subCentroids[4]);
	else
		subCentroids[4][c1] -= levelDown;
	subCentroids[5][c1] += levelDown;
	if (subCentroids[6][c2] < levelDown)
		markInvalid(subCentroids[6]);
	else
		subCentroids[6][c2] -= levelDown;
	subCentroids[7][c2] += levelDown;
#ifdef _DEBUG
	for (int i = 0; i < 8; ++i) {
		if (subCentroids[i][0] == USHRT_MAX)  // || subCentroids[i][1] == USHRT_MAX || subCentroids[i][2] == USHRT_MAX)
			continue;
		assert(centroidUpOneLevel(subCentroids[i]) == macroCentroid);
	}
#endif
}

const bccTetCentroid vnBccTetrahedra::centroidUpOneLevel(const bccTetCentroid& tcIn) {
	bccTetCentroid tcUp;
	int levelBit, levelX2, levelX4, hc;  // levelBit = 1, level = getResolutionLevel(tcIn);
	centroidHalfAxisSize(tcIn, hc, levelBit);
//	for (int i = 1; i < level; ++i)
//		levelBit <<= 1;
	levelX2 = levelBit << 1;
	levelX4 = levelX2 << 1;
//	int hc = -1;
//	for (int i = 0; i < 3; ++i)
//		if (tcIn[i] & levelBit) {
//			hc = i;
//			break;
//		}
	int c1 = (hc + 1) % 3, c2 = (hc + 2) % 3;
	tcUp = tcIn;
	assert((tcUp[c1] & levelX2) != (tcUp[c2] & levelX2));
	// none of the 4 core subtets have the same hc as the supertet
	// if making tc[hc] a multiple of 4 with a one unit move creates a valid level up tet, is a center core subtet
	tcUp[hc] += (tcUp[hc] & levelX2) ? levelBit : -levelBit;
	if (tcUp[c1] & levelX2) {
		if ((tcUp[hc] & levelX4) != (tcUp[c2] & levelX4))  // valid level 2
			return tcUp;
	}
	if (tcUp[c2] & levelX2) {
		if ((tcUp[hc] & levelX4) != (tcUp[c1] & levelX4))  // valid level 2
			return tcUp;
	}
	// is corner subtet and not core so will have same hc axis in level 2
	tcUp[hc] = tcIn[hc];
	tcUp[hc] += tcUp[hc] & levelX2 ? -levelBit : levelBit;
	if (tcUp[c1] & levelX2) {
		if (tcUp[c2] & levelX4) {
			tcUp[c1] += tcUp[c1] & levelX4 ? levelX2 : -levelX2;
		}
		else {
			tcUp[c1] += tcUp[c1] & levelX4 ? -levelX2 : levelX2;
		}
	}
	else {
		assert(tcUp[c2] & levelX2);
		if (tcUp[c1] & levelX4) {
			tcUp[c2] += tcUp[c2] & levelX4 ? levelX2 : -levelX2;
		}
		else {
			tcUp[c2] += tcUp[c2] & levelX4 ? -levelX2 : levelX2;
		}
	}
	return tcUp;
}

int vnBccTetrahedra::faceAdjacentCentroid(const bccTetCentroid& tc, const int face, bccTetCentroid& tcAdj)
{	// triangle faces are listed cyclic from the 4 tet nodes. Face 0 and 2 are CW, 1 & 3 CCW.
	// Returns face # of the adjacent macrotet.  If adjacent centroid would be outside positive octant (illegal centroid), face return is -1.
	int adjFace;
	tcAdj = tc;
	int ha, aha, size;
	centroidHalfAxisSize(tc, ha, size);
	tcAdj[ha] -= size;
	if (face < 1 || face >2) {
		aha = (ha + 2) % 3;
		tcAdj[aha] += size;
		if (((tc[ha] + tc[aha]) >> 1) & size) {  // down tet
			tcAdj[ha] += 2 * size;
			adjFace = 2;
			if (face < size) {
				if (tcAdj[aha] < 2 * size)
					return -1;
				tcAdj[aha] -= 2 * size;
			}
		}
		else {
			adjFace = 1;
			if (face > 2) {
				if (tcAdj[aha] < 2 * size)
					return -1;
				tcAdj[aha] -= 2 * size;
			}
		}
	}
	else {
		aha = (ha + 1) % 3;
		tcAdj[aha] += size;
		if (face > 1) {
			if (tcAdj[aha] < 2 * size)
				return -1;
			tcAdj[aha] -= 2 * size;
		}
		if (((tc[ha] + tc[aha]) >> 1) & size) {  // up tet
			tcAdj[ha] += 2 * size;
			adjFace = face > 1 ? 0 : 3;
		}
		else
			adjFace = face > 1 ? 3 : 0;
	}
	return adjFace;
}

vnBccTetrahedra::vnBccTetrahedra() : _nodeSpatialCoords(nullptr), _firstInteriorTet(-1)
{
}


vnBccTetrahedra::~vnBccTetrahedra()
{
}
