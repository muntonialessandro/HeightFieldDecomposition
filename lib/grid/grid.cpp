#include "grid.h"

//#define CUBE_CENTROID 1

using namespace cg3;

Grid::Grid() {
}

Grid::Grid(const Point3i& resolution, const Array3D<Point3d>& gridCoordinates, const Array3D<gridreal>& signedDistances, const Point3d& gMin, const Point3d& gMax) :
    signedDistances(signedDistances), target(0,0,0) {
    unit = gridCoordinates(1,0,0).x() - gridCoordinates(0,0,0).x();
    bb.setMin(gMin);
    bb.setMax(gMax);
    resX = resolution.x();
    resY = resolution.y();
    resZ = resolution.z();
    weights = Array3D<gridreal>(resX,resY,resZ, BORDER_PAY);
    for (unsigned int i = 2; i < resX-2; i++){
        for (unsigned int j = 2; j < resY-2; j++){
            for (unsigned int k = 2; k < resZ-2; ++k){
                weights(i,j,k) = STD_PAY;
            }
        }
    }
    coeffs = std::vector<std::array<gridreal, 64> >(1);
    mapCoeffs = Array3D<int>((resX-1),(resY-1),(resZ-1), 0);
    //coeffs = Array4D<gridreal>((resX-1),(resY-1),(resZ-1),64, 0);
}

/**
 * @brief Grid::calculateWeights
 *
 * Calcola i pesi per gli heightfieltds rispetto alla normale target
 * @param d
 */
void Grid::calculateBorderWeights(const Dcel& d, bool tolerance, std::set<const Dcel::Face*>& savedFaces) {
	cgal::AABBTree3 aabb(d);
    double unit = getUnit();
	std::vector<Point3i> flipped;
	std::vector<Point3i> notFlipped;
    for (unsigned int i = 0; i < resX-1; i++){
        for (unsigned int j = 0; j < resY-1; j++){
            for (unsigned int k = 0; k < resZ-1; k++){
                #ifdef CUBE_CENTROID
                CG3_SUPPRESS_WARNING(tolerance);
				Point3d bbmin = getPoint(i,j,k) - (unit/2);
				Point3d bbmax = getPoint(i,j,k) + (unit/2);
                #else
				Point3d bbmin = getPoint(i,j,k);
				Point3d bbmax(bbmin.x()+unit, bbmin.y()+unit, bbmin.z()+unit);
                #endif
				BoundingBox3 bb(bbmin, bbmax);
                std::list<const Dcel::Face*> l;
				aabb.containedDcelFaces(l, bb);
                if (l.size() != 0){
                    bool b = true;
                    for (std::list<const Dcel::Face*>::iterator it = l.begin(); it != l.end(); ++it) {
                        const Dcel::Face* f = *it;
						Point3i p(i,j,k);
						if (f->normal().dot(target) < FLIP_ANGLE && f->flag() != 1 && savedFaces.find(f) == savedFaces.end()){
                             flipped.push_back(p);
                             b = false;
                        }
                        ///
                        else {
                            notFlipped.push_back(p);
                        }
                        ///
                    }
                    if (b){
                        #ifdef CUBE_CENTROID
                        weights(i,j,k) = MIN_PAY;
                        #else
                        setWeightOnCube(i,j,k, MIN_PAY);
                        #endif
                    }
                    #ifdef CUBE_CENTROID
                    else {
                        weights(i,j,k) = MAX_PAY;
                    }
                    #endif
                }
            }
        }
    }
    #ifndef CUBE_CENTROID
    if (tolerance){
        for (unsigned int i = 0; i < flipped.size(); ++i){
            setWeightOnCube(flipped[i].x(),flipped[i].y(),flipped[i].z(), MAX_PAY);
        }
        ///
        for (unsigned int i = 0; i < notFlipped.size(); ++i){
            setWeightOnCube(notFlipped[i].x(),notFlipped[i].y(),notFlipped[i].z(), MIN_PAY);
        }
        ///
    }
    else {
        ///
        for (unsigned int i = 0; i < notFlipped.size(); ++i){
            setWeightOnCube(notFlipped[i].x(),notFlipped[i].y(),notFlipped[i].z(), MIN_PAY);
        }
        ///
        for (unsigned int i = 0; i < flipped.size(); ++i){
            setWeightOnCube(flipped[i].x(),flipped[i].y(),flipped[i].z(), MAX_PAY);
        }
    }
    #endif
}

/**
 * @brief Grid::freezeKernel
 *
 * Assegna massimo peso ai punti nel kernel
 * @param value should be a number between 0 and 1
 */
void Grid::calculateWeightsAndFreezeKernel(const Dcel& d, double value, bool tolerance, std::set<const Dcel::Face*>& savedFaces) {
    assert(value >= 0 && value <= 1);
    // grid border and rest
    weights.fill(BORDER_PAY);
    for (unsigned int i = 2; i < resX-2; i++){
        for (unsigned int j = 2; j < resY-2; j++){
            for (unsigned int k = 2; k < resZ-2; ++k){
                weights(i,j,k) = STD_PAY;
            }
        }
    }

    //mesh border
    calculateBorderWeights(d, tolerance, savedFaces);

    double minValue = signedDistances.min();
    value = 1 - value;
    value *= minValue;
    value = std::abs(value);

    //kernel
    for (unsigned int i = 0; i < getResX(); ++i){
        for (unsigned int j = 0; j < getResY(); ++j){
            for (unsigned int k = 0; k < getResZ(); ++k){
                if (getSignedDistance(i,j,k) < -value){
                    weights(i,j,k) = MAX_PAY;
                }
            }
        }
    }
    TricubicInterpolator::getCoefficients(coeffs, mapCoeffs, weights);
}

void Grid::calculateFullBoxValues(double (*integralTricubicInterpolation)(const gridreal *&, double, double, double, double, double, double)) {
    fullBoxValues = Array3D<gridreal>(getResX()-1, getResY()-1, getResZ()-1);
    #pragma omp parallel for
	for (unsigned int i = 0; i < fullBoxValues.sizeX(); ++i){
		for (unsigned int j = 0; j < fullBoxValues.sizeY(); ++j){
			for (unsigned int k = 0; k < fullBoxValues.sizeZ(); ++k){
                const gridreal * coeffs;
                getCoefficients(coeffs, i, j, k);
                fullBoxValues(i,j,k) = integralTricubicInterpolation(coeffs, 0,0,0,1,1,1);
            }
        }
    }
}

double Grid::getValue(const Point3d& p) const {
    if (! bb.isStrictlyIntern(p)) return BORDER_PAY;
    unsigned int xi = getIndexOfCoordinateX(p.x()), yi = getIndexOfCoordinateY(p.y()), zi = getIndexOfCoordinateZ(p.z());
	Point3d n = getPoint(xi, yi, zi);
    if (n == p)
        return weights(xi,yi,zi);
    else{
        n = (p - n) / getUnit(); // n ora è un punto nell'intervallo 0 - 1
        int id = mapCoeffs(xi, yi, zi);
        const gridreal* coef = coeffs[id].data();
        return TricubicInterpolator::getValue(n, coef);
    }
}

void Grid::getMinAndMax(double& min, double& max) {
    min = MIN_PAY; max = MAX_PAY;
	for (double xi = bb.minX(); xi <= bb.maxX(); xi+=0.5){
		for (double yi = bb.minY(); yi <= bb.maxY(); yi+=0.5){
			for (double zi = bb.minZ(); zi <= bb.maxZ(); zi+=0.5){
				double w = getValue(Point3d(xi,yi,zi));
                if (w < min){
                    min = w;
                }
                if (w > max){
                    max = w;
                }
            }
        }
    }
}

void Grid::serialize(std::ofstream& binaryFile) const {
    serializeObjectAttributes("Grid", binaryFile, bb, resX, resY, resZ,
                                          signedDistances, weights, coeffs, mapCoeffs,
                                          fullBoxValues, target, unit);
}

void Grid::deserialize(std::ifstream& binaryFile) {
    deserializeObjectAttributes("Grid", binaryFile, bb, resX, resY, resZ,
                                          signedDistances, weights, coeffs, mapCoeffs,
                                          fullBoxValues, target, unit);
}


