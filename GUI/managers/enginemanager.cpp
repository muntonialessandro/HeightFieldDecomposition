#include "enginemanager.h"
#include "ui_enginemanager.h"
#include "common.h"
#include <cstdio>
#include <QMessageBox>
#include <omp.h>
#include "cg3/cgal/aabb_tree3.h"
#include "engine/packing.h"
#include "engine/reconstruction.h"
#include <QThread>
#include <cg3/meshes/eigenmesh/algorithms/eigenmesh_algorithms.h>
#include <engine/tinyfeaturedetection.h>
#include <cg3/geometry/transformations3.h>
#include <cg3/utilities/set.h>
#include <cg3/libigl/remove_duplicate_vertices.h>


using namespace cg3;

EngineManager::EngineManager(QWidget *parent) :
    QFrame(parent),
    ui(new Ui::EngineManager),
    mainWindow((cg3::viewer::MainWindow&)*parent),
    g(nullptr),
    d(nullptr),
    b(nullptr),
    b2(nullptr),
    iterations(nullptr),
    solutions(nullptr),
    baseComplex(nullptr),
    he(nullptr),
    alreadySplitted(false){

    ui->setupUi(this);
    ui->iterationsSlider->setMaximum(0);
    hfdls.addSupportedExtension("hfd", "bin");

    binls.addSupportedExtension("bin");

    objls.addSupportedExtension("obj");

    //ui->frame_2->setVisible(false);
    //ui->frame_5->setVisible(false);
}

void EngineManager::deleteDrawableObject(DrawableObject* d) {
    if (d != nullptr) {
        //d->setVisible(false);
        mainWindow.deleteDrawableObject(d);
        delete d;
        d = nullptr;
    }
}

EngineManager::~EngineManager() {
    delete ui;
    deleteDrawableObject(g);
    deleteDrawableObject(d);
    deleteDrawableObject(b);
    deleteDrawableObject(b2);
    deleteDrawableObject(iterations);
    deleteDrawableObject(solutions);
    deleteDrawableObject(baseComplex);
    deleteDrawableObject(he);
}

void EngineManager::updateLabel(double value, QLabel* label) {
    std::stringstream ss;
    ss << std::setprecision(std::numeric_limits<double>::digits10+1);
    ss << value;
    label->setText(QString::fromStdString(ss.str()));
}

void EngineManager::updateBoxValues() {
    if (b != nullptr){
		ui->minXSpinBox->setValue(b->minX());
		ui->minYSpinBox->setValue(b->minY());
		ui->minZSpinBox->setValue(b->minZ());
		ui->maxXSpinBox->setValue(b->maxX());
		ui->maxYSpinBox->setValue(b->maxY());
		ui->maxZSpinBox->setValue(b->maxZ());
		ui->wSpinBox->setValue(b->maxX() - b->minX());
		ui->hSpinBox->setValue(b->maxY() - b->minY());
		ui->dSpinBox->setValue(b->maxZ() - b->minZ());
    }
}

void EngineManager::updateColors(double angleThreshold, double areaThreshold) {
	d->setFaceColors(Color(128,128,128));
    std::set<const Dcel::Face*> flippedFaces, savedFaces;
    Engine::getFlippedFaces(flippedFaces, savedFaces, *d, XYZ[ui->targetComboBox->currentIndex()], angleThreshold/100, areaThreshold);

    for (const Dcel::Face* cf : flippedFaces){
		Dcel::Face* f = d->face(cf->id());
        f->setColor(Color(255,0,0));
    }
    for (const Dcel::Face* cf : savedFaces){
		Dcel::Face* f = d->face(cf->id());
        f->setColor(Color(0,0,255));
    }

    d->update();
    mainWindow.canvas.update();
}

Point3d EngineManager::getLimits() {
    assert(d!=nullptr);
	BoundingBox3 bb = d->boundingBox();
	double lx = bb.lengthX();
	double ly = bb.lengthY();
	double lz = bb.lengthZ();
    double min = lx;
    if (lx <= ly && lx <= lz){
        min = lx;
    }
    else if (ly <= lx && ly <= lz) {
        min = ly;
    }
    else if (lz <= lx && lz <= ly) {
        min = lz;
    } else assert(0);
    double minreal = ui->minEdgeBBSpinBox->value();
    double limitRealX = ui->xLimitSpinBox->value();
    double limitRealY = ui->xLimitSpinBox->value();
    double limitRealZ = ui->zLimitSpinBox->value();
	Point3d limits;
    limits.x() = (limitRealX * min) / minreal;
    limits.y() = (limitRealY * min) / minreal;
    limits.z() = (limitRealZ * min) / minreal;
    return limits;
}

void EngineManager::saveMSCFile(const std::string& filename, const Dcel& d, const BoxList& bl) {
	Array2D<int> mat(bl.getNumberBoxes(), d.numberFaces(), 0);
	cgal::AABBTree3 tree(d);
    for (unsigned int i = 0; i < bl.getNumberBoxes(); i++){
		std::list<const Dcel::Face*> list = tree.completelyContainedDcelFaces(bl.getBox(i));
        for (const Dcel::Face* f : list){
			mat(i,f->id()) = 1;
        }
    }
    std::ofstream of;
    of.open(filename);
	of << mat.sizeX() << " " << mat.sizeY() << "\n";
	for (unsigned int i = 0; i < mat.sizeX(); i++){
		for (unsigned int j =0; j < mat.sizeY(); j++){
            of << mat(i,j) << " ";
        }
        of << "x\n";
    }
    of.close();
}

void EngineManager::serializeBC(const std::string &filename) {
    std::ofstream myfile;
    myfile.open (filename, std::ios::out | std::ios::binary);


    serialize(myfile);
    serializeObjectAttributes("HFDAfterBooleans", myfile, *baseComplex, *he, originalSolutions, splittedBoxesToOriginals, priorityBoxes);

    myfile.close();
}

void EngineManager::deserializeBC(const std::string &filename) {
    std::ifstream myfile;
    myfile.open (filename, std::ios::in | std::ios::binary);
    cg3::DrawableEigenMesh tmpbc;
    HeightfieldsList tmphe;
    try {
        deserialize(myfile);
        deserializeObjectAttributes("HFDAfterBooleans", myfile, tmpbc, tmphe, originalSolutions, splittedBoxesToOriginals, priorityBoxes);
        baseComplex = new cg3::DrawableEigenMesh(tmpbc);
        he = new HeightfieldsList(tmphe);
        ui->solutionNumberLabel->setText(QString::fromStdString(std::to_string(he->getNumHeightfields())));

        mainWindow.pushDrawableObject(baseComplex, "Base Complex");
        mainWindow.pushDrawableObject(he, "Heightfields");
        mainWindow.canvas.update();
        mainWindow.canvas.fitScene();
        ui->showAllSolutionsCheckBox->setEnabled(true);

        solutions->setVisibleBox(0);
        ui->heightfieldsSlider->setMaximum(he->getNumHeightfields()-1);
        ui->allHeightfieldsCheckBox->setChecked(true);
        ui->solutionsSlider->setEnabled(true);
        ui->solutionsSlider->setMaximum(solutions->getNumberBoxes()-1);
        ui->setFromSolutionSpinBox->setValue(0);
        ui->setFromSolutionSpinBox->setMaximum(solutions->getNumberBoxes()-1);

        for (unsigned int i : priorityBoxes){
            std::string s = ui->listLabel->text().toStdString();
            ui->listLabel->setText(QString::fromStdString(s + std::to_string(i) + "; "));
        }

        mainWindow.setDrawableObjectVisibility(d, false);
        mainWindow.setDrawableObjectVisibility(&originalMesh, false);
        mainWindow.setDrawableObjectVisibility(solutions, false);
        mainWindow.setDrawableObjectVisibility(baseComplex, false);
        //BU
        /*Eigen::MatrixXd m = Common::getRotationMatrix(Vec3(0,0,1), M_PI/2);
        he->rotate(m);
        d->rotate(m);
        originalMesh.rotate(m);
        baseComplex->rotate(m);

        m = Common::getRotationMatrix(Vec3(0,1,0), M_PI);
        he->rotate(m);
        d->rotate(m);
        d->updateFaceNormals();
        d->updateVertexNormals();
        d->updateBoundingBox();
        originalMesh.rotate(m);
        originalMesh.updateVertexAndFaceNormals();
        baseComplex->rotate(m);*/
        //

        //Statue
        /*Eigen::MatrixXd m = Common::getRotationMatrix(Vec3(-1,0,0), M_PI/2);
        he->rotate(m);
        d->rotate(m);
        originalMesh.rotate(m);
        baseComplex->rotate(m);

        m = Common::getRotationMatrix(Vec3(0,1,0), M_PI);
        he->rotate(m);
        d->rotate(m);
        d->updateFaceNormals();
        d->updateVertexNormals();
        d->updateBoundingBox();
        originalMesh.rotate(m);
        originalMesh.updateVertexAndFaceNormals();
        baseComplex->rotate(m);*/
        //

        //Airplane
        /*Eigen::MatrixXd m = Common::getRotationMatrix(Vec3(-1,0,0), M_PI/2);
        he->rotate(m);
        d->rotate(m);
        originalMesh.rotate(m);
        baseComplex->rotate(m);

        m = Common::getRotationMatrix(Vec3(0,1,0), M_PI/2);
        he->rotate(m);
        d->rotate(m);
        d->updateFaceNormals();
        d->updateVertexNormals();
        d->updateBoundingBox();
        originalMesh.rotate(m);
        originalMesh.updateVertexAndFaceNormals();
        baseComplex->rotate(m);*/
        //

        //Batman
        /*Eigen::MatrixXd m = Common::getRotationMatrix(Vec3(-1,0,0), M_PI/2);
        he->rotate(m);
        d->rotate(m);
        d->updateFaceNormals();
        d->updateVertexNormals();
        d->updateBoundingBox();
        originalMesh.rotate(m);
        originalMesh.updateVertexAndFaceNormals();
        baseComplex->rotate(m);*/
        //

        //Fertility
        /*Eigen::MatrixXd m = Common::getRotationMatrix(Vec3(-1,0,0), M_PI/2);
        he->rotate(m);
        d->rotate(m);
        d->updateFaceNormals();
        d->updateVertexNormals();
        d->updateBoundingBox();
        originalMesh.rotate(m);
        originalMesh.updateVertexAndFaceNormals();
        baseComplex->rotate(m);*/

        //Bunny
        /*Eigen::MatrixXd m = Common::getRotationMatrix(Vec3(-1,0,0), M_PI/2);
        he->rotate(m);
        d->rotate(m);
        d->updateFaceNormals();
        d->updateVertexNormals();
        d->updateBoundingBox();
        originalMesh.rotate(m);
        originalMesh.updateVertexAndFaceNormals();
        baseComplex->rotate(m);*/
        //
    }
    catch(std::ios_base::failure& e) {
        std::cerr << e.what();
    }
    myfile.close();
}

void EngineManager::setBinPath(const std::string &path) {
    binls.setActualPath(path);
}

void EngineManager::setHfdPath(const std::string &path) {
    hfdls.setActualPath(path);
}

void EngineManager::setObjPath(const std::string &path) {
    objls.setActualPath(path);
}

void EngineManager::serialize(std::ofstream& binaryFile) const {
    if (d != nullptr && solutions != nullptr){
        double factor = ui->factorSpinBox->value();
        double kernel = ui->distanceSpinBox->value();
        serializeObjectAttributes("HFDBeforeSplitting", binaryFile, *d, *solutions, originalMesh, factor, kernel);
    }
}

void EngineManager::deserialize(std::ifstream& binaryFile) {
    DrawableDcel tmpd;
    BoxList tmpsol;
    double factor, kernel;
    try {
        deserializeObjectAttributes("HFDBeforeSplitting", binaryFile, tmpd, tmpsol, originalMesh, factor, kernel);
        d = new DrawableDcel(std::move(tmpd));
        solutions = new BoxList(std::move(tmpsol));
        ui->factorSpinBox->setValue(factor);
        ui->distanceSpinBox->setValue(kernel);

        ////
        mainWindow.pushDrawableObject(d, "Input Mesh");
        mainWindow.pushDrawableObject(&originalMesh, "Original Mesh");
        mainWindow.pushDrawableObject(solutions, "Solutions");

        d->setWireframe(true);
        d->setPointsShading();
        d->update();
        ui->weigthsRadioButton->setChecked(true);
        ui->sliceCheckBox->setChecked(true);
        //sol
        solutions->setVisibleBox(0);
        solutions->setCylinders(false);
        ui->showAllSolutionsCheckBox->setEnabled(true);
        ui->solutionsSlider->setEnabled(true);
        ui->solutionsSlider->setMaximum(solutions->getNumberBoxes()-1);
        ui->setFromSolutionSpinBox->setValue(0);
        ui->setFromSolutionSpinBox->setMaximum(solutions->getNumberBoxes()-1);

        //factor and kernel
        ui->factorSpinBox->setValue(factor);
        ui->distanceSpinBox->setValue(kernel);
    }
    catch(std::exception& e){
        std::cerr << "ERROR reading file.\n";
        std::cerr << e.what() << "\n";
    }
}

void EngineManager::on_generateGridPushButton_clicked() {
    if (d != nullptr){
        deleteDrawableObject(g);
        g = new DrawableGrid();
		BoundingBox3 bb= d->boundingBox();
        Engine::scaleAndRotateDcel(*d, 0, ui->factorSpinBox->value());
		originalMesh.scale(bb, d->boundingBox());
        std::set<const Dcel::Face*> flippedFaces, savedFaces;
        Engine::getFlippedFaces(flippedFaces, savedFaces, *d, XYZ[ui->targetComboBox->currentIndex()], (double)ui->toleranceSlider->value()/100, ui->areaToleranceSpinBox->value());
        Engine::generateGrid(*g, *d, ui->distanceSpinBox->value(), ui->heightfieldsCheckBox->isChecked(), XYZ[ui->targetComboBox->currentIndex()], savedFaces);
        updateColors(ui->toleranceSlider->value(), ui->areaToleranceSpinBox->value());
        d->update();
        g->updateMinSignedDistance();
        g->setKernelDistance(ui->distanceSpinBox->value());
        e = Energy(*g);
        mainWindow.pushDrawableObject(g, "Grid");
        mainWindow.canvas.update();
    }
}

void EngineManager::on_distanceSpinBox_valueChanged(double arg1) {
    if (g!=nullptr){
        g->setKernelDistance(arg1);
        mainWindow.canvas.update();
    }
}

void EngineManager::on_targetComboBox_currentIndexChanged(int index) {
    if (d!= nullptr && g!= nullptr){
        g->setTarget(XYZ[index]);
        updateColors(ui->toleranceSlider->value(), ui->areaToleranceSpinBox->value());
        //std::set<const Dcel::Face*> flippedFaces, savedFaces;
        //Engine::getFlippedFaces(flippedFaces, savedFaces, *d, XYZ[ui->targetComboBox->currentIndex()], (double)ui->toleranceSlider->value()/100, ui->areaToleranceSpinBox->value());
        //g->calculateBorderWeights(*d, ui->heightfieldsCheckBox->isChecked(), savedFaces);
        mainWindow.canvas.update();
    }
}

void EngineManager::on_kernelRadioButton_toggled(bool checked) {
    if (checked && g!=nullptr){
        g->setDrawKernel();
        mainWindow.canvas.update();
    }
}

void EngineManager::on_weigthsRadioButton_toggled(bool checked) {
    if (checked && g!=nullptr){
        g->setDrawBorders();
        mainWindow.canvas.update();
    }
}

void EngineManager::on_freezeKernelPushButton_clicked() {
    if (g!=nullptr && d!=nullptr){
        double value = ui->distanceSpinBox->value();
        g->setTarget(XYZ[ui->targetComboBox->currentIndex()]);
        std::set<const Dcel::Face*> flippedFaces, savedFaces;
        Engine::getFlippedFaces(flippedFaces, savedFaces, *d, XYZ[ui->targetComboBox->currentIndex()], (double)ui->toleranceSlider->value()/100, ui->areaToleranceSpinBox->value());
        g->calculateWeightsAndFreezeKernel(*d, value, ui->heightfieldsCheckBox->isChecked(), savedFaces);
        e = Energy(*g);
        e.calculateFullBoxValues(*g);
        mainWindow.canvas.update();
    }

}

void EngineManager::on_sliceCheckBox_stateChanged(int arg1) {
    if (g!=nullptr){
        if (arg1 == Qt::Checked){
            ui->sliceComboBox->setEnabled(true);
            ui->sliceSlider->setEnabled(true);
            int s = ui->sliceComboBox->currentIndex();
            g->setSliceValue(0);
            g->setSlice(s+1);
            if (s == 0) ui->sliceSlider->setMaximum(g->getResX() -1);
            if (s == 1) ui->sliceSlider->setMaximum(g->getResY() -1);
            if (s == 2) ui->sliceSlider->setMaximum(g->getResZ() -1);
        }
        else {
            ui->sliceComboBox->setEnabled(false);
            ui->sliceSlider->setEnabled(false);
            g->setSlice(0);
        }
        mainWindow.canvas.update();
    }
}

void EngineManager::on_sliceSlider_valueChanged(int value) {
    if (g!=nullptr){
        g->setSliceValue(value);
        mainWindow.canvas.update();
    }
}

void EngineManager::on_sliceComboBox_currentIndexChanged(int index) {
    if (g!=nullptr){
        g->setSliceValue(0);
        g->setSlice(index+1);
        ui->sliceSlider->setValue(0);
        if (index == 0) ui->sliceSlider->setMaximum(g->getResX() -1);
        if (index == 1) ui->sliceSlider->setMaximum(g->getResY() -1);
        if (index == 2) ui->sliceSlider->setMaximum(g->getResZ() -1);
        mainWindow.canvas.update();
    }
}

void EngineManager::on_serializePushButton_clicked() {
    std::string filename = binls.saveDialog();
    if (filename != ""){
        std::ofstream myfile;
        myfile.open (filename, std::ios::out | std::ios::binary);
        //serializeOld(myfile);
        serialize(myfile);
        myfile.close();
    }
}

void EngineManager::on_deserializePushButton_clicked() {
    std::string filename = binls.loadDialog();
    if (filename != ""){
        on_cleanAllPushButton_clicked();
        std::ifstream myfile;
        myfile.open (filename, std::ios::in | std::ios::binary);
        //deserializeOld(myfile);
        deserialize(myfile);
        myfile.close();
    }
}

void EngineManager::on_saveObjsButton_clicked() {
    if (d != nullptr && baseComplex != nullptr && he != nullptr) {
        std::string foldername = objls.directoryDialog();
        if (foldername != ""){
            d->updateVertexNormals();
            Engine::saveObjs(foldername, originalMesh, *d, *baseComplex, *he);

            markerMesh.saveOnObj(foldername + "/Marker.obj");

            //smart packing
            HeightfieldsList myHe = *he;
            Packing::rotateAllPieces(myHe);
			BoundingBox3 packSize(Point3d(), Point3d(d->boundingBox().lengthX(), d->boundingBox().lengthX() * (3.0/4.0), d->boundingBox().lengthX() * (10.0/4.0)));
			BoundingBox3 limits = packSize;
			double l = packSize.maxY();
            std::vector< std::vector<EigenMesh> > packs;
            do {
                limits.setMaxY(l);
                double factor;
                Packing::getMaximum(myHe, limits, factor);
                Packing::scaleAll(myHe, factor);
				std::vector< std::vector<std::pair<int, Point3d> > > tmp = Packing::pack(myHe, packSize);
                packs = Packing::getPacks(tmp, myHe);
				l -= packSize.maxY() / 100;
            } while (packs.size() > 1);
            //EigenMeshAlgorithms::makeBox(packSize).saveOnObj(foldername + "/box.obj");

            for (unsigned int i = 0; i < packs.size(); i++){
                std::string pstring = foldername + "/pack.obj";
                EigenMesh packMesh = packs[i][0];
                //packs[i][0].saveOnObj(foldername + "/p0b0.obj");
                for (unsigned int j = 1; j < packs[i].size(); j++){
                    //packs[i][j].saveOnObj(foldername + "/p0b" + std::to_string(j) + ".obj");
                    packMesh = EigenMesh::merge(packMesh, packs[i][j]);
                }
                //packMesh.saveOnObj(pstring.toStdString());
                Dcel d(packMesh);
                //Eigen::MatrixXd m = Common::getRotationMatrix(Vec3(-1,0,0), M_PI/2);
                //d.rotate(m);
				d.saveOnObj(pstring);
            }
        }
    }
}

void EngineManager::on_wSpinBox_valueChanged(double arg1) {
    if (b!=nullptr){
        b->setW(arg1);
        updateBoxValues();
        mainWindow.canvas.update();
    }
}

void EngineManager::on_hSpinBox_valueChanged(double arg1) {
    if (b!=nullptr){
        b->setH(arg1);
        updateBoxValues();
        mainWindow.canvas.update();
    }
}

void EngineManager::on_dSpinBox_valueChanged(double arg1) {
    if (b!=nullptr){
        b->setD(arg1);
        updateBoxValues();
        mainWindow.canvas.update();
    }
}

void EngineManager::on_plusXButton_clicked() {
    if (b!=nullptr){
        if (ui->boxRadioButton->isChecked()){
            b->moveX(ui->stepSpinBox->value());
            updateBoxValues();
        }
        else if (ui->box2RadioButton->isChecked()){
            b2->moveX(ui->stepSpinBox->value());
        }
        else {
			Point3d c;
            if (ui->c1RadioButton->isChecked()){
                c = b->getConstraint1();
				b->setConstraint1(Point3d(c.x()+ui->stepSpinBox->value(), c.y(), c.z()));
            }
            if (ui->c2RadioButton->isChecked()){
                c = b->getConstraint2();
				b->setConstraint2(Point3d(c.x()+ui->stepSpinBox->value(), c.y(), c.z()));
            }
            if (ui->c3RadioButton->isChecked()){
                c = b->getConstraint3();
				b->setConstraint3(Point3d(c.x()+ui->stepSpinBox->value(), c.y(), c.z()));
            }
            if (ui->allRadioButton->isChecked()){
                b->moveX(ui->stepSpinBox->value());
                c = b->getConstraint1();
				b->setConstraint1(Point3d(c.x()+ui->stepSpinBox->value(), c.y(), c.z()));
                c = b->getConstraint2();
				b->setConstraint2(Point3d(c.x()+ui->stepSpinBox->value(), c.y(), c.z()));
                c = b->getConstraint3();
				b->setConstraint3(Point3d(c.x()+ui->stepSpinBox->value(), c.y(), c.z()));
            }
        }

        mainWindow.canvas.update();
    }
}

void EngineManager::on_minusXButton_clicked() {
    if (b!=nullptr){
        if (ui->boxRadioButton->isChecked()){
            b->moveX(- ui->stepSpinBox->value());
            updateBoxValues();
        }
        else if (ui->box2RadioButton->isChecked()){
            b2->moveX(- ui->stepSpinBox->value());
        }
        else {
			Point3d c;
            if (ui->c1RadioButton->isChecked()){
                c = b->getConstraint1();
				b->setConstraint1(Point3d(c.x()-ui->stepSpinBox->value(), c.y(), c.z()));
            }
            if (ui->c2RadioButton->isChecked()){
                c = b->getConstraint2();
				b->setConstraint2(Point3d(c.x()-ui->stepSpinBox->value(), c.y(), c.z()));
            }
            if (ui->c3RadioButton->isChecked()){
                c = b->getConstraint3();
				b->setConstraint3(Point3d(c.x()-ui->stepSpinBox->value(), c.y(), c.z()));
            }
            if (ui->allRadioButton->isChecked()){
                b->moveX(- ui->stepSpinBox->value());
                c = b->getConstraint1();
				b->setConstraint1(Point3d(c.x()-ui->stepSpinBox->value(), c.y(), c.z()));
                c = b->getConstraint2();
				b->setConstraint2(Point3d(c.x()-ui->stepSpinBox->value(), c.y(), c.z()));
                c = b->getConstraint3();
				b->setConstraint3(Point3d(c.x()-ui->stepSpinBox->value(), c.y(), c.z()));
            }
        }
        mainWindow.canvas.update();
    }
}

void EngineManager::on_plusYButton_clicked() {
    if (b!=nullptr){
        if (ui->boxRadioButton->isChecked()){
            b->moveY(ui->stepSpinBox->value());
            updateBoxValues();
        }
        else if (ui->box2RadioButton->isChecked()){
            b2->moveY(ui->stepSpinBox->value());
        }
        else {
			Point3d c;
            if (ui->c1RadioButton->isChecked()){
                c = b->getConstraint1();
				b->setConstraint1(Point3d(c.x(), c.y()+ui->stepSpinBox->value(), c.z()));
            }
            if (ui->c2RadioButton->isChecked()){
                c = b->getConstraint2();
				b->setConstraint2(Point3d(c.x(), c.y()+ui->stepSpinBox->value(), c.z()));
            }
            if (ui->c3RadioButton->isChecked()){
                c = b->getConstraint3();
				b->setConstraint3(Point3d(c.x(), c.y()+ui->stepSpinBox->value(), c.z()));
            }
            if (ui->allRadioButton->isChecked()){
                b->moveY(ui->stepSpinBox->value());
                c = b->getConstraint1();
				b->setConstraint1(Point3d(c.x(), c.y()+ui->stepSpinBox->value(), c.z()));
                c = b->getConstraint2();
				b->setConstraint2(Point3d(c.x(), c.y()+ui->stepSpinBox->value(), c.z()));
                c = b->getConstraint3();
				b->setConstraint3(Point3d(c.x(), c.y()+ui->stepSpinBox->value(), c.z()));
            }

        }
        mainWindow.canvas.update();
    }
}

void EngineManager::on_minusYButton_clicked() {
    if (b!=nullptr){
        if (ui->boxRadioButton->isChecked()){
            b->moveY(- ui->stepSpinBox->value());
            updateBoxValues();
        }
        else if (ui->box2RadioButton->isChecked()){
            b2->moveY(- ui->stepSpinBox->value());
        }
        else {
			Point3d c;
            if (ui->c1RadioButton->isChecked()){
                c = b->getConstraint1();
				b->setConstraint1(Point3d(c.x(), c.y()-ui->stepSpinBox->value(), c.z()));
            }
            if (ui->c2RadioButton->isChecked()){
                c = b->getConstraint2();
				b->setConstraint2(Point3d(c.x(), c.y()-ui->stepSpinBox->value(), c.z()));
            }
            if (ui->c3RadioButton->isChecked()){
                c = b->getConstraint3();
				b->setConstraint3(Point3d(c.x(), c.y()-ui->stepSpinBox->value(), c.z()));
            }
            if (ui->allRadioButton->isChecked()){
                b->moveY(- ui->stepSpinBox->value());
                c = b->getConstraint1();
				b->setConstraint1(Point3d(c.x(), c.y()-ui->stepSpinBox->value(), c.z()));
                c = b->getConstraint2();
				b->setConstraint2(Point3d(c.x(), c.y()-ui->stepSpinBox->value(), c.z()));
                c = b->getConstraint3();
				b->setConstraint3(Point3d(c.x(), c.y()-ui->stepSpinBox->value(), c.z()));
            }

        }
        mainWindow.canvas.update();
    }
}

void EngineManager::on_plusZButton_clicked() {
    if (b!=nullptr){
        if (ui->boxRadioButton->isChecked()){
            b->moveZ(ui->stepSpinBox->value());
            updateBoxValues();
        }
        else if (ui->box2RadioButton->isChecked()){
            b2->moveZ(ui->stepSpinBox->value());
        }
        else {
			Point3d c;
            if (ui->c1RadioButton->isChecked()){
                c = b->getConstraint1();
				b->setConstraint1(Point3d(c.x(), c.y(), c.z()+ui->stepSpinBox->value()));
            }
            if (ui->c2RadioButton->isChecked()){
                c = b->getConstraint2();
				b->setConstraint2(Point3d(c.x(), c.y(), c.z()+ui->stepSpinBox->value()));
            }
            if (ui->c3RadioButton->isChecked()){
                c = b->getConstraint3();
				b->setConstraint3(Point3d(c.x(), c.y(), c.z()+ui->stepSpinBox->value()));
            }
            if (ui->allRadioButton->isChecked()){
                b->moveZ(ui->stepSpinBox->value());
                c = b->getConstraint1();
				b->setConstraint1(Point3d(c.x(), c.y(), c.z()+ui->stepSpinBox->value()));
                c = b->getConstraint2();
				b->setConstraint2(Point3d(c.x(), c.y(), c.z()+ui->stepSpinBox->value()));
                c = b->getConstraint3();
				b->setConstraint3(Point3d(c.x(), c.y(), c.z()+ui->stepSpinBox->value()));
            }

        }
        mainWindow.canvas.update();
    }
}

void EngineManager::on_minusZButton_clicked() {
    if (b!=nullptr){
        if (ui->boxRadioButton->isChecked()){
            b->moveZ(- ui->stepSpinBox->value());
            updateBoxValues();
        }
        else if (ui->box2RadioButton->isChecked()){
            b2->moveZ(- ui->stepSpinBox->value());
        }
        else {
			Point3d c;
            if (ui->c1RadioButton->isChecked()){
                c = b->getConstraint1();
				b->setConstraint1(Point3d(c.x(), c.y(), c.z()-ui->stepSpinBox->value()));
            }
            if (ui->c2RadioButton->isChecked()){
                c = b->getConstraint2();
				b->setConstraint2(Point3d(c.x(), c.y(), c.z()-ui->stepSpinBox->value()));
            }
            if (ui->c3RadioButton->isChecked()){
                c = b->getConstraint3();
				b->setConstraint3(Point3d(c.x(), c.y(), c.z()-ui->stepSpinBox->value()));
            }
            if (ui->allRadioButton->isChecked()){
                b->moveZ(- ui->stepSpinBox->value());
                c = b->getConstraint1();
				b->setConstraint1(Point3d(c.x(), c.y(), c.z()-ui->stepSpinBox->value()));
                c = b->getConstraint2();
				b->setConstraint2(Point3d(c.x(), c.y(), c.z()-ui->stepSpinBox->value()));
                c = b->getConstraint3();
				b->setConstraint3(Point3d(c.x(), c.y(), c.z()-ui->stepSpinBox->value()));
            }

        }
        mainWindow.canvas.update();
    }
}

void EngineManager::on_energyBoxPushButton_clicked() {
    if (b!=nullptr){
        double energy = e.energy(*b);
        Eigen::VectorXd gradient(6);
        Eigen::VectorXd solution(6);
		solution << b->minX(), b->minY(), b->minZ(), b->maxX(), b->maxY(), b->maxZ();
        e.gradientEnergy(gradient, solution, b->getConstraint1(), b->getConstraint2(), b->getConstraint3());
        updateLabel(gradient(0), ui->gminx);
        updateLabel(gradient(1), ui->gminy);
        updateLabel(gradient(2), ui->gminz);
        updateLabel(gradient(3), ui->gmaxx);
        updateLabel(gradient(4), ui->gmaxy);
        updateLabel(gradient(5), ui->gmaxz);
        std::cerr << "\nGradient: \n" << gradient << "\n";
        updateLabel(energy, ui->energyLabel);
        mainWindow.canvas.update();
    }
}

void EngineManager::on_minimizePushButton_clicked() {
    if (b!=nullptr){
        double it;
        if (ui->saveIterationsCheckBox->isChecked()){
            deleteDrawableObject(iterations);
            iterations = new BoxList();
            Timer t("Graidient Discent");
            it = e.gradientDiscend(*b, *iterations);
            t.stopAndPrint();
            iterations->setVisibleBox(0);
            ui->iterationsSlider->setMaximum(iterations->getNumberBoxes()-1);
            mainWindow.pushDrawableObject(iterations, "Iterations");

            double energy = e.energy(iterations->getBox(0));
            updateLabel(energy, ui->energyIterationLabel);
        }
        else {
            Timer t("Gradient Discent");
            it = e.gradientDiscend(*b);
            t.stopAndPrint();
        }
        updateLabel(it, ui->minimizedEnergyLabel);
        double energy = e.energy(*b);
        updateLabel(energy, ui->energyLabel);
        updateBoxValues();
        mainWindow.canvas.update();
    }
}

void EngineManager::on_BFGSButton_clicked() {
    if (b!=nullptr){
        double it;
        if (ui->saveIterationsCheckBox->isChecked()){
            deleteDrawableObject(iterations);
            iterations = new BoxList();
            Timer t("BFGS");
            it = e.BFGS(*b, *iterations);
            t.stopAndPrint();
            iterations->setVisibleBox(0);
            ui->iterationsSlider->setMaximum(iterations->getNumberBoxes()-1);
            mainWindow.pushDrawableObject(iterations, "Iterations");

            double energy = e.energy(iterations->getBox(0));
            updateLabel(energy, ui->energyIterationLabel);
        }
        else {
            Timer t("BFGS");
            it = e.BFGS(*b);
            t.stopAndPrint();
        }
        updateLabel(it, ui->minimizedEnergyLabel);
        double energy = e.energy(*b);
        updateLabel(energy, ui->energyLabel);
        updateBoxValues();
        mainWindow.canvas.update();
    }
}


void EngineManager::on_serializeBoxPushButton_clicked() {
    if (b!=nullptr){
        std::ofstream myfile;
        myfile.open ("box.bin", std::ios::out | std::ios::binary);
        b->serialize(myfile);
        myfile.close();
    }
}

void EngineManager::on_deserializeBoxPushButton_clicked() {
    if (b == nullptr){
        b = new Box3D();
        mainWindow.pushDrawableObject(b, "Box", false);
    }

    std::ifstream myfile;
    myfile.open ("box.bin", std::ios::in | std::ios::binary);
    b->deserialize(myfile);
    myfile.close();
    updateBoxValues();
    mainWindow.canvas.update();
}

void EngineManager::on_iterationsSlider_sliderMoved(int position) {
    if (iterations != nullptr){
        iterations->setVisibleBox(position);
        mainWindow.canvas.update();
    }
}

void EngineManager::on_energyIterationsButton_clicked() {
    if (iterations != nullptr){
        Box3D b = iterations->getBox(ui->iterationsSlider->value());
        double energy = e.energy(b);
        Eigen::VectorXd gradient(6);
		e.gradientTricubicInterpolationEnergy(gradient, b.min(), b.max());
        std::cerr << "Gradient: \n" << gradient << "\n";
        updateLabel(energy, ui->energyIterationLabel);
    }
}

void EngineManager::on_showAllSolutionsCheckBox_stateChanged(int arg1) {
    if (arg1 == Qt::Checked){
        ui->solutionsSlider->setEnabled(false);
        ui->solutionsSlider->setValue(0);
        solutions->setCylinders(true);
        solutions->setVisibleBox(-1);
    }
    else {
        ui->solutionsSlider->setEnabled(true);
        ui->solutionsSlider->setValue(0);
        solutions->setCylinders(false);
        solutions->setVisibleBox(0);
    }
    ui->solutionNumberLabel->setNum((int)solutions->getNumberBoxes());
    mainWindow.canvas.update();
}

void EngineManager::on_solutionsSlider_valueChanged(int value) {
    if (solutions != nullptr){
        if (ui->solutionsSlider->isEnabled() && value >= 0){
            solutions->setVisibleBox(value);
            ui->setFromSolutionSpinBox->setValue(value);
            ui->solutionNumberLabel->setText(QString::number((*solutions)[value].getId()));
            mainWindow.canvas.update();
        }
    }
}

void EngineManager::on_setFromSolutionButton_clicked() {
    if (solutions != nullptr){
        unsigned int value = ui->setFromSolutionSpinBox->value();
        if (value < solutions->getNumberBoxes()){
            if (b == nullptr){
                b = new Box3D(solutions->getBox(value));
                mainWindow.pushDrawableObject(b, "Box");
            }
			b->setMin(solutions->getBox(value).min());
			b->setMax(solutions->getBox(value).max());
            b->setConstraint1(solutions->getBox(value).getConstraint1());
            b->setConstraint2(solutions->getBox(value).getConstraint2());
            b->setConstraint3(solutions->getBox(value).getConstraint3());
            b->setColor(solutions->getBox(value).getColor());
            b->setRotationMatrix(solutions->getBox(value).getRotationMatrix());
            b->setTarget(solutions->getBox(value).getTarget());
            //deleteDrawableObject(b);
            //solutions->getBox(value);
            //b = new Box3D(solutions->getBox(value));
            //mainWindow.pushObj(b, "Box");
            mainWindow.canvas.update();
        }

    }
    else if (d != nullptr) {
        unsigned int value = ui->setFromSolutionSpinBox->value();
		if (value < d->numberFaces()) {
			const Dcel::Face* f = d->face(value);
            if (b == nullptr){
                b = new Box3D(solutions->getBox(value));
                mainWindow.pushDrawableObject(b, "Box");
            }
            Box3D box;
			box.setTarget(nearestNormal(f->normal()));
			Point3d p1 = f->outerHalfEdge()->fromVertex()->coordinate();
			Point3d p2 = f->outerHalfEdge()->toVertex()->coordinate();
			Point3d p3 = f->outerHalfEdge()->next()->toVertex()->coordinate();
			Point3d bmin = p1;
            bmin = bmin.min(p2);
            bmin = bmin.min(p3);
            bmin = bmin - 1;
			Point3d bmax = p1;
            bmax = bmax.max(p2);
            bmax = bmax.max(p3);
            bmax = bmax + 1;
            box.setMin(bmin);
            box.setMax(bmax);
			box.setColor(colorOfNearestNormal(f->normal()));
            box.setConstraint1(p1);
            box.setConstraint2(p2);
            box.setConstraint3(p3);
            *b = box;
        }
    }
}

void EngineManager::on_wireframeDcelCheckBox_stateChanged(int arg1) {
    if (d!=nullptr && ui->inputMeshRadioButton->isChecked()){
        d->setWireframe(arg1 == Qt::Checked);
        mainWindow.canvas.update();
    }
    if (baseComplex!=nullptr && ui->baseComplexRadioButton->isChecked()) {
        baseComplex->setWireframe(arg1 == Qt::Checked);
        mainWindow.canvas.update();
    }
    if (he != nullptr && ui->heightfieldsRadioButton->isChecked()){
        he->setWireframe(arg1 == Qt::Checked);
        mainWindow.canvas.update();
    }
	if (originalMesh.numberVertices() > 0 && ui->originalMeshRadioButton->isChecked()){
        originalMesh.setWireframe(arg1 == Qt::Checked);
        mainWindow.canvas.update();
    }
}

void EngineManager::on_pointsDcelRadioButton_toggled(bool checked) {
    if (d!=nullptr && ui->inputMeshRadioButton->isChecked()){
        if (checked){
            d->setPointsShading();
            mainWindow.canvas.update();
        }
    }
    if (baseComplex!=nullptr && ui->baseComplexRadioButton->isChecked()) {
        if (checked){
            baseComplex->setPointsShading();
            mainWindow.canvas.update();
        }
    }
    if (he != nullptr && ui->heightfieldsRadioButton->isChecked()){
        if (checked){
            he->setPointShading();
            mainWindow.canvas.update();
        }
    }
	if (originalMesh.numberVertices() > 0 && ui->originalMeshRadioButton->isChecked()){
        if (checked){
            originalMesh.setPointsShading();
            mainWindow.canvas.update();
        }
    }
}

void EngineManager::on_flatDcelRadioButton_toggled(bool checked) {
    if (d!=nullptr && ui->inputMeshRadioButton->isChecked()){
        if (checked){
            d->setFlatShading();
            mainWindow.canvas.update();
        }
    }
    if (baseComplex!=nullptr && ui->baseComplexRadioButton->isChecked()) {
        if (checked){
            baseComplex->setFlatShading();
            mainWindow.canvas.update();
        }
    }
    if (he != nullptr && ui->heightfieldsRadioButton->isChecked()){
        if (checked){
            he->setFlatShading();
            mainWindow.canvas.update();
        }
    }
	if (originalMesh.numberVertices() > 0 && ui->originalMeshRadioButton->isChecked()){
        if (checked){
            originalMesh.setFlatShading();
            mainWindow.canvas.update();
        }
    }
}

void EngineManager::on_smoothDcelRadioButton_toggled(bool checked) {
    if (d!=nullptr && ui->inputMeshRadioButton->isChecked()){
        if (checked){
            d->setSmoothShading();
            mainWindow.canvas.update();
        }
    }
    if (baseComplex!=nullptr && ui->baseComplexRadioButton->isChecked()) {
        if (checked){
            baseComplex->setSmoothShading();
            mainWindow.canvas.update();
        }
    }
    if (he != nullptr && ui->heightfieldsRadioButton->isChecked()){
        if (checked){
            he->setSmoothShading();
            mainWindow.canvas.update();
        }
    }
	if (originalMesh.numberVertices() > 0 && ui->originalMeshRadioButton->isChecked()){
        if (checked){
            originalMesh.setSmoothShading();
            mainWindow.canvas.update();
        }
    }
}

void EngineManager::on_trianglesCoveredPushButton_clicked() {
    if (d!=nullptr && b != nullptr) {
        Eigen::Matrix3d m[ORIENTATIONS];
        Eigen::Matrix3d mb = b->getRotationMatrix();
        m[0] = Eigen::Matrix3d::Identity();
        #if ORIENTATIONS > 1
        getRotationMatrix(Vec3(0,0,-1), 0.785398, m[1]);
        getRotationMatrix(Vec3(-1,0,0), 0.785398, m[2]);
        getRotationMatrix(Vec3(0,-1,0), 0.785398, m[3]);
        #endif
        Dcel dd = *d;
        std::list<const Dcel::Face*> covered;
        if (mb == m[0]){
			cgal::AABBTree3 t(dd);
			t.containedDcelFaces(covered, *b);
        }
        #if ORIENTATIONS > 1
        else if (mb == m[1]){

            Eigen::Matrix3d mm;
            getRotationMatrix(Vec3(0,0,1), 0.785398, mm);
            dd.rotate(mm);
            CGALInterface::AABBTree t(dd);
            t.getIntersectedDcelFaces(covered, *b);
        }
        else if (mb == m[2]){
            Eigen::Matrix3d mm;
            getRotationMatrix(Vec3(1,0,0), 0.785398, mm);
            dd.rotate(mm);
            CGALInterface::AABBTree t(dd);
            t.getIntersectedDcelFaces(covered, *b);
        }
        else if (mb == m[3]){
            Eigen::Matrix3d mm;
            getRotationMatrix(Vec3(0,1,0), 0.785398, mm);
            dd.rotate(mm);
            CGALInterface::AABBTree t(dd);
            t.getIntersectedDcelFaces(covered, *b);
        }
        #endif
        else assert(0);


        std::list<const Dcel::Face*>::iterator i = covered.begin();
        while (i != covered.end()) {
            const Dcel::Face* f = *i;
			Point3d p1 = f->vertex1()->coordinate(), p2 = f->vertex2()->coordinate(), p3 = f->vertex3()->coordinate();

            if (!b->isIntern(p1) || !b->isIntern(p2) || !b->isIntern(p3)) {
                i =covered.erase(i);
            }
            else ++i;
        }


        for (std::list<const Dcel::Face*>::iterator it = covered.begin(); it != covered.end(); ++it){
            const Dcel::Face* cf = *it;
			Dcel::Face* f = d->face(cf->id());
            f->setColor(Color(0,0,255));
        }
        d->update();

        mainWindow.canvas.update();

    }
}

void EngineManager::on_stepDrawGridSpinBox_valueChanged(double arg1) {
    if (g!=nullptr){
        if (arg1 > 0){
            g->setStepDrawGrid(arg1);
            mainWindow.canvas.update();
        }
    }
}

void EngineManager::on_subtractPushButton_clicked() {
    if (solutions!= nullptr && d != nullptr){
        deleteDrawableObject(baseComplex);
        EigenMesh m = (Dcel)*d;
        baseComplex = new DrawableEigenMesh(m);
        mainWindow.pushDrawableObject(baseComplex, "Base Complex");
        deleteDrawableObject(he);
        //deleteDrawableObject(entirePieces);
        he = new HeightfieldsList();
        //entirePieces = new HeightfieldsList();
        mainWindow.pushDrawableObject(he, "Heightfields");
        //mainWindow.pushObj(entirePieces, "Entire Pieces");
        mainWindow.canvas.update();
        SimpleEigenMesh bc((SimpleEigenMesh)*baseComplex);

        /*WaitDialog* dialog = new WaitDialog("Prova", mainWindow);
        connect(this, SIGNAL(finished()), dialog, SLOT(pop()));

        dialog->show();
        EngineWorker* ew = new EngineWorker();
        QThread* thread = new QThread;
        ew->moveToThread(thread);
        connect(ew, SIGNAL (error(QString)), this, SLOT (errorString(QString)));
        connect(thread, SIGNAL (started()), ew, SLOT (booleanOperations(*he, bc, *solutions, *d)));
        connect(ew, SIGNAL (finished()), thread, SLOT (quit()));
        connect(ew, SIGNAL (finished()), ew, SLOT (deleteLater()));
        connect(thread, SIGNAL (finished()), thread, SLOT (deleteLater()));
        connect(thread, SIGNAL (finished()), this, SLOT(emit finished()));
        thread->start();*/

        ///cleaning solutions
        Timer tBooleans("Total Booleans Time");
        Engine::booleanOperations(*he, bc, *solutions, false);
        Engine::splitConnectedComponents(*he, *solutions, splittedBoxesToOriginals);
        Engine::glueInternHeightfieldsToBaseComplex(*he, *solutions, bc, *d);
		cgal::AABBTree3 tree(*d);
        Engine::updatePiecesNormals(tree, *he);
        tBooleans.stopAndPrint();
        ui->showAllSolutionsCheckBox->setEnabled(true);
        solutions->setVisibleBox(0);
        ui->heightfieldsSlider->setMaximum(he->getNumHeightfields()-1);
        ui->allHeightfieldsCheckBox->setChecked(true);
        ui->solutionsSlider->setEnabled(true);
        ui->solutionsSlider->setMaximum(solutions->getNumberBoxes()-1);
        ui->setFromSolutionSpinBox->setValue(0);
        ui->setFromSolutionSpinBox->setMaximum(solutions->getNumberBoxes()-1);
        mainWindow.deleteDrawableObject(baseComplex);
        delete baseComplex;
        baseComplex = new DrawableEigenMesh(bc);
        baseComplex->updateFaceNormals();
		for (unsigned int i = 0; i < baseComplex->numberFaces(); ++i){
			Vec3d n = baseComplex->faceNormal(i);
            n.normalize();
            Color c = colorOfNearestNormal(n);
            baseComplex->setFaceColor(c.redF(), c.greenF(), c.blueF(), i);
        }
        mainWindow.pushDrawableObject(baseComplex, "Base Complex");
        mainWindow.canvas.update();
    }
}

void EngineManager::on_stickPushButton_clicked() {
    if (d!=nullptr && baseComplex != nullptr && he != nullptr){
        SimpleEigenMesh bc = *baseComplex;
        Engine::reduceHeightfields(*he, bc, *d);
        deleteDrawableObject(baseComplex);
        baseComplex = new DrawableEigenMesh(bc);
        baseComplex->updateFaceNormals();
		for (unsigned int i = 0; i < baseComplex->numberFaces(); ++i){
			Vec3d n = baseComplex->faceNormal(i);
            n.normalize();
            Color c = colorOfNearestNormal(n);
            baseComplex->setFaceColor(c.redF(), c.greenF(), c.blueF(), i);
        }
        mainWindow.pushDrawableObject(baseComplex, "Base Complex");
    }
    mainWindow.canvas.update();
}

void EngineManager::on_serializeBCPushButton_clicked() {
    if (baseComplex != nullptr && solutions != nullptr && d != nullptr && he != nullptr /*&& entirePieces != nullptr*/){
        std::string fn = hfdls.saveDialog();
        if (fn != "")
            //serializeBCOld(fn);
            serializeBC(fn);
    }

}

void EngineManager::on_deserializeBCPushButton_clicked() {
    std::string fn = hfdls.loadDialog();
    if (fn != "") {
        on_cleanAllPushButton_clicked();
        //deserializeBCOld(fn);
        deserializeBC(fn);
    }
}

void EngineManager::on_createAndMinimizeAllPushButton_clicked() {
    if (g == nullptr && d!= nullptr) {
		BoundingBox3 bb = d->boundingBox();
        Engine::scaleAndRotateDcel(*d, 0, ui->factorSpinBox->value());
		if (originalMesh.numberVertices() > 0){
			originalMesh.scale(bb, d->boundingBox());
        }
        std::set<const Dcel::Face*> flippedFaces, savedFaces;
        Engine::getFlippedFaces(flippedFaces, savedFaces, *d, XYZ[ui->targetComboBox->currentIndex()], (double)ui->toleranceSlider->value()/100, ui->areaToleranceSpinBox->value());
        updateColors(ui->toleranceSlider->value(), ui->areaToleranceSpinBox->value());
        d->update();
        mainWindow.canvas.update();
    }
    if (d!=nullptr){
        deleteDrawableObject(solutions);
        solutions = new BoxList();
        mainWindow.pushDrawableObject(solutions, "Solutions");
        double kernelDistance = ui->distanceSpinBox->value();
        Timer t("Total Time Grids and Minimization Boxes");
        /// here d is already scaled!!
        if (ui->limitsConstraintCheckBox->isChecked()){
            Engine::optimizeAndDeleteBoxes(*solutions, *d, kernelDistance, true, getLimits(), ui->heightfieldsCheckBox->isChecked(), ui->onlyNearestTargetCheckBox->isChecked(), ui->areaToleranceSpinBox->value(), (double)ui->toleranceSlider->value()/100, ui->useFileCheckBox->isChecked());
        }
        else {
			Engine::optimizeAndDeleteBoxes(*solutions, *d, kernelDistance, false, Point3d(), ui->heightfieldsCheckBox->isChecked(), ui->onlyNearestTargetCheckBox->isChecked(), ui->areaToleranceSpinBox->value(), (double)ui->toleranceSlider->value()/100, ui->useFileCheckBox->isChecked());
        }
        t.stopAndPrint();
        ui->showAllSolutionsCheckBox->setEnabled(true);
        solutions->setVisibleBox(0);
        ui->solutionsSlider->setEnabled(true);
        ui->solutionsSlider->setMaximum(solutions->getNumberBoxes()-1);
        ui->setFromSolutionSpinBox->setValue(0);
        ui->setFromSolutionSpinBox->setMaximum(solutions->getNumberBoxes()-1);
        mainWindow.canvas.update();
    }
}

void EngineManager::on_allHeightfieldsCheckBox_stateChanged(int arg1) {
    if (arg1 == Qt::Checked){
        if (he != nullptr){
            he->setVisibleHeightfield(-1);
            //if (entirePieces != nullptr) entirePieces->setVisibleHeightfield(-1);
            mainWindow.canvas.update();
        }
    }
    else {
        if (he != nullptr){
            //if (entirePieces != nullptr) entirePieces->setVisibleHeightfield(ui->heightfieldsSlider->value());
            he->setVisibleHeightfield(ui->heightfieldsSlider->value());
            mainWindow.canvas.update();
        }
    }
}

void EngineManager::on_heightfieldsSlider_valueChanged(int value) {
    if (he != nullptr){
        ui->solutionsSlider->setValue(value);
        //if (entirePieces != nullptr) entirePieces->setVisibleHeightfield(value);
        he->setVisibleHeightfield(value);
        mainWindow.canvas.update();

        std::cerr << "Box id: " << solutions->getBox(value).getId() << "\n";
        std::cerr << "Generated by: " << splittedBoxesToOriginals[solutions->getBox(value).getId()] << "\n";

        ui->prioritySpinBox->setValue(splittedBoxesToOriginals[solutions->getBox(value).getId()]);
    }
}

void EngineManager::on_minXSpinBox_valueChanged(double arg1) {
    if (b!=nullptr){
        b->setMinX(arg1);
        mainWindow.canvas.update();
    }
}

void EngineManager::on_minYSpinBox_valueChanged(double arg1) {
    if (b!=nullptr){
        b->setMinY(arg1);
        mainWindow.canvas.update();
    }
}

void EngineManager::on_minZSpinBox_valueChanged(double arg1) {
    if (b!=nullptr){
        b->setMinZ(arg1);
        mainWindow.canvas.update();
    }
}

void EngineManager::on_maxXSpinBox_valueChanged(double arg1) {
    if (b!=nullptr){
        b->setMaxX(arg1);
        mainWindow.canvas.update();
    }
}

void EngineManager::on_maxYSpinBox_valueChanged(double arg1) {
    if (b!=nullptr){
        b->setMaxY(arg1);
        mainWindow.canvas.update();
    }
}

void EngineManager::on_maxZSpinBox_valueChanged(double arg1) {
    if (b!=nullptr){
        b->setMaxZ(arg1);
        mainWindow.canvas.update();
    }
}

void EngineManager::on_toleranceSlider_valueChanged(int value) {
    if (d!= nullptr){
        updateColors(value, ui->areaToleranceSpinBox->value());
    }
}

void EngineManager::on_areaToleranceSpinBox_valueChanged(double arg1){
    if (d!= nullptr){
        updateColors(ui->toleranceSlider->value(), arg1);
    }
}

void EngineManager::on_cleanAllPushButton_clicked() {
    deleteDrawableObject(g);
    deleteDrawableObject(d);
    deleteDrawableObject(b);
    deleteDrawableObject(iterations);
    deleteDrawableObject(solutions);
    deleteDrawableObject(baseComplex);
    deleteDrawableObject(he);
    deleteDrawableObject(b2);
	if (originalMesh.numberVertices()>0){
        mainWindow.deleteDrawableObject(&originalMesh);
        originalMesh.clear();
    }
    g = nullptr;
    d = nullptr;
    b = nullptr;
    iterations = nullptr;
    solutions = nullptr;
    baseComplex = nullptr;
    he = nullptr;
    b2 = nullptr;
    markerMesh.clear();

    originalSolutions.clearBoxes();
    alreadySplitted = false;
    splittedBoxesToOriginals.clear();
    priorityBoxes.clear();
    //deleteDrawableObject(entirePieces);
}

void EngineManager::on_reorderBoxes_clicked() {
    if (d != nullptr && solutions != nullptr){


        Timer tGraph("Total Time Graph optimization");
        Array2D<int> ordering = Splitting::getOrdering(*solutions, *d, splittedBoxesToOriginals, priorityBoxes, userArcs);
        tGraph.stopAndPrint();
        //solutions->setIds();
        solutions->sort(ordering);
        for (unsigned int i = 0; i < solutions->getNumberBoxes(); i++){
            std::cerr << solutions->getBox(i).getId() << " ";
            //solutions->getBox(i).getEigenMesh().saveOnObj("ob" + std::to_string(i) + "_" + std::to_string(solutions->getBox(i).getId()) + ".obj");
        }
        for (std::pair<unsigned int, unsigned int> p : splittedBoxesToOriginals){
            std::cerr << "\nBox " << p.first << " splitted from Box " <<  p.second ;
        }
        std::cerr << "\n";
        ui->solutionsSlider->setMaximum(solutions->getNumberBoxes()-1);
        mainWindow.canvas.update();
    }
}

void EngineManager::on_loadOriginalPushButton_clicked() {
    std::string filename = objls.loadDialog();
    if (filename != "") {
		originalMesh.loadFromObj(filename);
        if (! (mainWindow.containsDrawableObject(&originalMesh)))
            mainWindow.pushDrawableObject(&originalMesh, filename.substr(filename.find_last_of("/") + 1));
        mainWindow.canvas.update();
    }
}

void EngineManager::on_loadSmoothedPushButton_clicked() {
    std::string filename = objls.loadDialog();

    if (filename != "") {
        deleteDrawableObject(d);
        d = new DrawableDcel();
        if (d->loadFromFile(filename)){
            d->updateVertexNormals();
            d->setWireframe(true);
            d->setPointsShading();
            d->update();
            mainWindow.pushDrawableObject(d, filename.substr(filename.find_last_of("/") + 1));
            mainWindow.canvas.update();
        }
        else {
            delete d;
            d = nullptr;
            QMessageBox msgBox;
            msgBox.setText("File Format not supported.");
            msgBox.exec();
        }
    }
}

void EngineManager::on_taubinPushButton_clicked() {
    /*if (d == nullptr){
        d = new DrawableDcel(Reconstruction::taubinSmoothing(originalMesh, ui->nItersSpinBox->value(), ui->lambdaSpinBox->value(), ui->muSpinBox->value()));
        d->setWireframe(true);
        d->setPointsShading();
        d->update();
        mainWindow.pushObj(d, "Smoothed Mesh");
        mainWindow.canvas.update();
    }
    else {
        *d = DrawableDcel(Reconstruction::taubinSmoothing(*d, ui->nItersSpinBox->value(), ui->lambdaSpinBox->value(), ui->muSpinBox->value()));
        d->update();
        mainWindow.canvas.update();
    }*/
}

void EngineManager::on_packPushButton_clicked() {
    if (he != nullptr){
        std::string foldername = objls.directoryDialog();
        if (foldername != ""){
            HeightfieldsList myHe = *he;
            Packing::rotateAllPieces(myHe);
			BoundingBox3 packSize(Point3d(), Point3d(ui->sizeXPackSpinBox->value(), ui->sizeYPackSpinBox->value(), ui->sizeZPackSpinBox->value()));
			BoundingBox3 limits = packSize;
            switch(ui->limitComboBox->currentIndex()){
                case 1:
                    limits.setMaxX(ui->limitValueSpinBox->value());
                    break;
                case 2:
                    limits.setMaxY(ui->limitValueSpinBox->value());
                    break;
                case 3:
                    limits.setMaxZ(ui->limitValueSpinBox->value());
                    break;
            }
            double factor;
            Packing::getMaximum(myHe, limits, factor);
            Packing::scaleAll(myHe, factor);
			std::vector< std::vector<std::pair<int, Point3d> > > tmp = Packing::pack(myHe, packSize);
            std::vector< std::vector<EigenMesh> > packs = Packing::getPacks(tmp, myHe);
            EigenMeshAlgorithms::makeBox(packSize).saveOnObj(foldername + "/box.obj");
            for (unsigned int i = 0; i < packs.size(); i++){
                std::string pstring = foldername + "/pack" + std::to_string(i) + ".obj";
                EigenMesh packMesh = packs[i][0];
                std::string bstring = foldername + "/b" + std::to_string(i);
                for (unsigned int j = 0; j < packs[i].size(); j++){
                    std::string meshName = bstring + "p" + std::to_string(j) + ".obj";
                    //packs[i][j].saveOnObj(meshName.toStdString());
                    Dcel d(packs[i][j]);
                    d.updateVertexNormals();
					d.saveOnObj(meshName);
                    if (j > 0){
                        packMesh = EigenMesh::merge(packMesh, packs[i][j]);
                    }
                }
                //packMesh.saveOnObj(pstring.toStdString());
                Dcel d(packMesh);
                d.updateVertexNormals();
				d.saveOnObj(pstring);

                /*BoundingBox bb = d.getBoundingBox();
                SimpleEigenMesh plane;
                plane.addVertex(bb.getMinX(), bb.getMinY(), bb.getMinZ() - EPSILON);
                plane.addVertex(bb.getMaxX(), bb.getMinY(), bb.getMinZ() - EPSILON);
                plane.addVertex(bb.getMaxX(), bb.getMaxY(), bb.getMinZ() - EPSILON);
                plane.addVertex(bb.getMinX(), bb.getMaxY(), bb.getMinZ() - EPSILON);
                plane.addFace(0, 1, 2);
                plane.addFace(0, 2, 3);
                plane.saveOnObj(foldername.toStdString() + "/plane.obj");*/
            }
        }
    }
}

void EngineManager::on_smartPackingPushButton_clicked() {
    if (he != nullptr){
        std::string foldername = objls.directoryDialog();
        if (foldername != ""){
            HeightfieldsList myHe = *he;
            Packing::rotateAllPieces(myHe);
			BoundingBox3 packSize(Point3d(), Point3d(ui->sizeXPackSpinBox->value(), ui->sizeYPackSpinBox->value(), ui->sizeZPackSpinBox->value()));
			BoundingBox3 limits = packSize;
			double l = packSize.maxY();
            std::vector< std::vector<EigenMesh> > packs;
            do {
                limits.setMaxY(l);
                double factor;
                Packing::getMaximum(myHe, limits, factor);
                Packing::scaleAll(myHe, factor);
				std::vector< std::vector<std::pair<int, Point3d> > > tmp = Packing::pack(myHe, packSize);
                packs = Packing::getPacks(tmp, myHe);
                l -= 0.1;
            } while (packs.size() > 1);
            EigenMeshAlgorithms::makeBox(packSize).saveOnObj(foldername + "/box.obj");

            for (unsigned int i = 0; i < packs.size(); i++){
                std::string pstring = foldername + "/pack" + std::to_string(i) + ".obj";
                EigenMesh packMesh = packs[i][0];
                packs[i][0].saveOnObj(foldername + "/p0b0.obj");
                for (unsigned int j = 1; j < packs[i].size(); j++){
                    packs[i][j].saveOnObj(foldername + "/p0b" + std::to_string(j) + ".obj");
                    packMesh = EigenMesh::merge(packMesh, packs[i][j]);
                }
                //packMesh.saveOnObj(pstring.toStdString());
                Dcel d(packMesh);
                //Eigen::MatrixXd m = Common::getRotationMatrix(Vec3(-1,0,0), M_PI/2);
                //d.rotate(m);
                d.updateFaceNormals();
                d.updateVertexNormals();
				d.saveOnObj(pstring);
            }
        }
    }
}


void EngineManager::on_reconstructionPushButton_clicked() {
    if (d != nullptr && he != nullptr){
        std::vector< std::pair<int,int> > mapping = Reconstruction::getMapping(*d, *he);
        Reconstruction::reconstruction(*d, mapping, originalMesh, *solutions, ui->internToHFCheckBox->isChecked());
        d->update();
        mainWindow.canvas.update();
    }
}

void EngineManager::on_putBoxesAfterPushButton_clicked() {
    int nbox = ui->coveredTrianglesSpinBox->value();
    if (nbox >= 0 && solutions != nullptr){
        Box3D small = solutions->getBox(nbox);
        solutions->removeBox(nbox);
        solutions->addBox(small);
        mainWindow.canvas.update();
    }
}

void EngineManager::on_snappingPushButton_clicked() {
    if (solutions != nullptr && d != nullptr){
        double epsilon = ui->snappingSpinBox->value();

        /*for (unsigned int i = 0; i < solutions->getNumberBoxes()-1; i++){
            Box3D b= solutions->getBox(i);
            for (unsigned j = 0; j < 6; j++){
                std::cerr << std::setprecision(17) << "before: " << b(j) << "\n";
                b(j) = Common::truncate(b(j), 5);
                std::cerr << std::setprecision(17) << "after: " << b(j) << "\n";
            }
            solutions->setBox(i, b);
        }*/

        //snapping
        Engine::stupidSnapping(*d, *solutions, epsilon);

        //new: forced snapping
        Engine::smartSnapping(*d, *solutions);
        //if not smart snapping
        /*cgal::AABBTree tree(*d);
        solutions->calculateTrianglesCovered(tree);
        solutions->generatePieces();
        solutions->sortByTrianglesCovered();*/


        //merging
        Engine::merging(*d, *solutions);

        //setting ids
        solutions->sortByTrianglesCovered();
        solutions->setIds();
        if (! alreadySplitted){
            originalSolutions = *solutions;
            alreadySplitted = true;
        }

        ui->solutionsSlider->setMaximum(solutions->getNumberBoxes()-1);
        mainWindow.canvas.update();
    }


}

void EngineManager::on_colorPiecesPushButton_clicked() {
    if (he!=nullptr && d != nullptr){
        d->updateFaceNormals();
        d->updateVertexNormals();
        Engine::colorPieces(*d, *he);
    }
    mainWindow.canvas.update();
}

void EngineManager::on_deleteBoxesPushButton_clicked() {
    if (solutions != nullptr && d != nullptr){
        Engine::minimalCovering(*solutions, *d);

        ui->solutionsSlider->setMaximum(solutions->getNumberBoxes()-1);
        mainWindow.canvas.update();
    }
}

void EngineManager::on_pushButton_clicked() {
    if (g == nullptr && d!= nullptr) {
		BoundingBox3 bb = d->boundingBox();
        Engine::scaleAndRotateDcel(*d, 0, ui->factorSpinBox->value());
		if (originalMesh.numberVertices() > 0){
			originalMesh.scale(bb, d->boundingBox());
        }
        std::set<const Dcel::Face*> flippedFaces, savedFaces;
        Engine::getFlippedFaces(flippedFaces, savedFaces, *d, XYZ[ui->targetComboBox->currentIndex()], (double)ui->toleranceSlider->value()/100, ui->areaToleranceSpinBox->value());
        updateColors(ui->toleranceSlider->value(), ui->areaToleranceSpinBox->value());
        d->update();
        mainWindow.canvas.update();
    }
    if (d!=nullptr){
        deleteDrawableObject(solutions);
        solutions = new BoxList();
        mainWindow.pushDrawableObject(solutions, "Solutions");
        double kernelDistance = ui->distanceSpinBox->value();
        Timer t("Total Time Grids and Minimization Boxes");
        /// here d is already scaled!!
        if (ui->limitsConstraintCheckBox->isChecked()){
            Engine::optimize(*solutions, *d, kernelDistance, true, getLimits(), ui->heightfieldsCheckBox->isChecked(), ui->onlyNearestTargetCheckBox->isChecked(), ui->areaToleranceSpinBox->value(), (double)ui->toleranceSlider->value()/100, ui->useFileCheckBox->isChecked());
        }
        else {
			Engine::optimize(*solutions, *d, kernelDistance, false, Point3d(), ui->heightfieldsCheckBox->isChecked(), ui->onlyNearestTargetCheckBox->isChecked(), ui->areaToleranceSpinBox->value(), (double)ui->toleranceSlider->value()/100, ui->useFileCheckBox->isChecked());
        }
        t.stopAndPrint();
        ui->showAllSolutionsCheckBox->setEnabled(true);
        solutions->setVisibleBox(0);
        ui->solutionsSlider->setEnabled(true);
        ui->solutionsSlider->setMaximum(solutions->getNumberBoxes()-1);
        ui->setFromSolutionSpinBox->setValue(0);
        ui->setFromSolutionSpinBox->setMaximum(solutions->getNumberBoxes()-1);
        mainWindow.canvas.update();
    }
}

void EngineManager::on_limitsConstraintCheckBox_stateChanged(int arg1) {
    if (arg1 ==Qt::Checked){
        ui->xLimitSpinBox->setEnabled(true);
        ui->xLimitSpinBox->setEnabled(true);
        ui->zLimitSpinBox->setEnabled(true);
        ui->minEdgeBBSpinBox->setEnabled(true);
    }
    else {
        ui->xLimitSpinBox->setEnabled(false);
        ui->xLimitSpinBox->setEnabled(false);
        ui->zLimitSpinBox->setEnabled(false);
        ui->minEdgeBBSpinBox->setEnabled(false);
    }
}


void EngineManager::on_explodePushButton_clicked() {
    if (d != nullptr && he != nullptr){
        double dist = ui->explodeSpinBox->value();
		Point3d bc = d->barycenter();
        he->explode(bc, dist);
        mainWindow.canvas.update();
    }
}

void EngineManager::on_createBoxPushButton_clicked() {
    if (b == nullptr){
		b = new Box3D(Point3d(0,0,0), Point3d(3,3,3));
		b->setConstraint1(Point3d(1, 1.5, 1.5));
		b->setConstraint2(Point3d(1.5, 1.5, 1));
		b->setConstraint3(Point3d(1, 1.5, 1));
        b->setColor(Color(0,0,0));
        mainWindow.pushDrawableObject(b, "Box");
    }
    //deleteDrawableObject(b);
    //solutions->getBox(value);
    //b = new Box3D(solutions->getBox(value));
    //mainWindow.pushObj(b, "Box");
    mainWindow.canvas.update();
}

void EngineManager::on_coveredTrianglesPushButton_clicked() {
    if (solutions != nullptr && d != nullptr){
        for (Dcel::Face* f : d->faceIterator())
            f->setColor(Color(128,128,128));
        std::vector< std::set<const Dcel::Face*> > trianglesCovered (solutions->getNumberBoxes());
		cgal::AABBTree3 tree(*d);
        for (unsigned int i = 0; i < solutions->getNumberBoxes(); i++) {
			std::list<const Dcel::Face*> ltmp = tree.completelyContainedDcelFaces(solutions->getBox(i));
            std::set<const Dcel::Face*> tcbox(ltmp.begin(), ltmp.end());
            trianglesCovered[i] = tcbox;
        }
        int h = 0;
        int step = 240 / solutions->getNumberBoxes();
        Color c;
        for (unsigned int i = 0; i < solutions->getNumberBoxes(); i++) {
            c.setHsv(h, 255, 255);
            std::set<const Dcel::Face*> lonelyTriangles = trianglesCovered[i];
            for (unsigned int j = 0; j < solutions->getNumberBoxes(); j++){
                if (j != i){
                    lonelyTriangles = cg3::difference(lonelyTriangles, trianglesCovered[j]);
                }
            }
            //assert(lonelyTriangles.size() > 0);
            for (const Dcel::Face* f : lonelyTriangles){
				d->face(f->id())->setColor(c);
            }
            h+=step;
        }
        d->update();
		d->saveOnObj("boxes/MeshTriangles.obj");

        mainWindow.canvas.update();
    }

}

void EngineManager::on_drawBoxMeshCheckBox_stateChanged(int arg1) {
    if (solutions!=nullptr){
        solutions->visualizeEigenMeshBox(arg1==Qt::Checked);
        mainWindow.canvas.update();
    }
}

void EngineManager::on_markerMeshPushButton_clicked() {
    if (he != nullptr && d != nullptr){
        markerMesh = Engine::getMarkerMesh(*he, *d);
        markerMesh.setPointsShading();
        markerMesh.setWireframe(true);
        markerMesh.setWireframeWidth(4);
        mainWindow.pushDrawableObject(&markerMesh, "MarkerMesh");
    }
}

void EngineManager::on_splitConnectedComponentsPushButton_clicked() {
    if (d != nullptr && solutions != nullptr){
        Engine::boxPostProcessing(*solutions, *d);
    }
}

void EngineManager::on_globalOptimalOrientationPushButton_clicked() {
    if (d != nullptr){
        Engine::findOptimalOrientation(*d, originalMesh);
        d->update();
        mainWindow.canvas.fitScene();
        mainWindow.canvas.update();
    }
}

void EngineManager::on_experimentButton_clicked() {
    if (he != nullptr && solutions != nullptr && d != nullptr){
        Engine::mergePostProcessing(*he, *solutions, *baseComplex, *d, ui->mergeDownwardsCheckBox->isChecked());
        ui->solutionsSlider->setMaximum(solutions->getNumberBoxes()-1);
        ui->heightfieldsSlider->setMaximum(solutions->getNumberBoxes()-1);
        mainWindow.canvas.update();
    }

}

void EngineManager::on_createBox2PushButton_clicked() {
    if (b2 == nullptr){
		b2 = new Box3D(Point3d(0,0,0), Point3d(3,3,3));
		b2->setConstraint1(Point3d(1, 1.5, 1.5));
		b2->setConstraint2(Point3d(1.5, 1.5, 1));
		b2->setConstraint3(Point3d(1, 1.5, 1));
        b2->setColor(Color(0,0,0));
        mainWindow.pushDrawableObject(b2, "Box2");
    }
    //deleteDrawableObject(b);
    //solutions->getBox(value);
    //b = new Box3D(solutions->getBox(value));
    //mainWindow.pushObj(b, "Box");
    mainWindow.canvas.update();
}

void EngineManager::on_restoreBoxesPushButton_clicked() {
    deleteDrawableObject(solutions);
    deleteDrawableObject(baseComplex);
    deleteDrawableObject(he);
    baseComplex = nullptr;
    he = nullptr;
    solutions = new BoxList(originalSolutions);
    mainWindow.pushDrawableObject(solutions, "Boxes");
    ui->solutionsSlider->setMaximum(solutions->getNumberBoxes()-1);
    ui->solutionsSlider->setValue(0);
    splittedBoxesToOriginals.clear();
    mainWindow.canvas.update();
    mainWindow.canvas.fitScene();
}

void EngineManager::on_pushPriorityBoxButton_clicked() {
    std::string s = ui->listLabel->text().toStdString();
    ui->listLabel->setText(QString::fromStdString(std::to_string(ui->prioritySpinBox->value()) + "; " + s));
    priorityBoxes.push_front(ui->prioritySpinBox->value());
}

void EngineManager::on_clearPriorityPushButton_clicked() {
    priorityBoxes.clear();
    ui->listLabel->setText("");
}

void EngineManager::on_pushUserArcButton_clicked() {
    std::pair<unsigned int, unsigned int> p(ui->boxBSpinBox->value(), ui->boxASpinBox->value());
    userArcs.push_back(p);
    ui->userArcsLabel->setText(ui->userArcsLabel->text() + QString::fromStdString("(" + std::to_string(p.first) + "; " + std::to_string(p.second) + "); "));
}

void EngineManager::on_clearUserArcsPushButton_clicked() {
    userArcs.clear();
    ui->userArcsLabel->setText("");
}

void EngineManager::on_deleteBlockPushButton_clicked() {
    if (solutions != nullptr && he != nullptr){
        unsigned int index = solutions->getIndexOf(ui->deleteBlockSpinBox->value());
        solutions->removeBox(index);
        he->removeHeightfield(index);
        ui->solutionsSlider->setMaximum(solutions->getNumberBoxes()-1);
        ui->heightfieldsSlider->setMaximum(he->getNumHeightfields()-1);
        mainWindow.canvas.update();
    }
}

void EngineManager::on_sdfPushButton_clicked() {
    if (d != nullptr && he != nullptr && solutions != nullptr){
        struct op{
                bool operator()(const std::pair<double, unsigned int>& p1, const std::pair<double, unsigned int>& p2){
                    return p1.second < p2.second;
                }
        };
        std::vector<std::pair<double, unsigned int> > tinyPieces;
        for (unsigned int i = 0; i < he->getNumHeightfields(); i++){
            libigl::removeDuplicateVertices(he->getHeightfield(i));
            double mindist;
			if (TinyFeatureDetection::tinyFeaturePlane(he->getHeightfield(i),he->getTarget(i), d->averageHalfEdgesLength()*ui->thresholdTinyFeaturesSpinBox->value(), mindist)){
                tinyPieces.push_back(std::pair<double, unsigned int>(mindist, solutions->getBox(i).getId()));
                he->getHeightfield(i).setFaceColor(Color(255,0,0));
            }
        }

        /*if (tinyPieces.size() > 0)
            QMessageBox::warning(this, "Tiny Pieces", "Number tiny Pieces: " + QString::fromStdString(std::to_string(tinyPieces.size())));*/

        std::cerr << "Tiny Pieces:\n";
        for (std::pair<double, unsigned int> p :tinyPieces){
            std::cerr << p.second << "; ";
        }
        std::cerr << "\n";

        std::sort(tinyPieces.begin(), tinyPieces.end(), op());

        bool inserted = false;
        for (unsigned int i = 0; i < tinyPieces.size() && !inserted; i++){
            std::pair<double, unsigned int> p = tinyPieces[i];
            std::list<unsigned int>::iterator it = std::find(priorityBoxes.begin(), priorityBoxes.end(), splittedBoxesToOriginals.at(p.second));
            if (it == priorityBoxes.end()){
                priorityBoxes.push_front(splittedBoxesToOriginals.at(p.second));
                std::string s = ui->listLabel->text().toStdString();
                ui->listLabel->setText(QString::fromStdString(std::to_string(splittedBoxesToOriginals.at(p.second)) + "; " + s));
                inserted = true;
            }
        }
        if (!inserted && tinyPieces.size() > 0)
            QMessageBox::warning(this, "Tiny Pieces", "It is not possible to avoid Tiny Pieces on this decomposition.");

        /*std::list<unsigned int> old = priorityBoxes;

        priorityBoxes.clear();

        for (std::pair<double, unsigned int> p : tinyPieces) {
            unsigned int id = solutions->getBox(p.second).getId();
            priorityBoxes.push_back(splittedBoxesToOriginals[id]);
        }

        std::list<unsigned int> tmp;
        for (unsigned int id : old) {
            std::list<unsigned int>::iterator it = std::find(priorityBoxes.begin(), priorityBoxes.end(), id);
            if (it == priorityBoxes.end()){
                tmp.push_back(id);
            }
        }

        priorityBoxes.insert(priorityBoxes.end(), tmp.begin(), tmp.end());
        ui->listLabel->setText("");
        for (unsigned int id : priorityBoxes){
            std::string s = ui->listLabel->text().toStdString();
            ui->listLabel->setText(QString::fromStdString(s + (s == "" ? "" : "; ") + std::to_string(id)));
        }*/
    }
    mainWindow.canvas.update();
}

void EngineManager::on_deleteBoxPushButton_clicked() {
    if (b != nullptr){
        deleteDrawableObject((b));
        b = nullptr;
    }
}

void EngineManager::on_deleteBox2PushButton_clicked() {
    if (b2 != nullptr){
        deleteDrawableObject((b2));
        b2 = nullptr;
    }
}

void EngineManager::on_saveShownBlockPushButton_clicked() {
    if (he != nullptr){
        int i = ui->heightfieldsSlider->value();
        he->getHeightfield(i).saveOnObj("Block.obj");
    }
}

void EngineManager::on_saveShownBoxPushButton_clicked() {
    if (solutions != nullptr){
        int i = ui->solutionsSlider->value();
        (*solutions)[i].getEigenMesh().saveOnObj("Box.obj");
    }
}

void EngineManager::on_rotatePushButton_clicked() {
    if (he != nullptr && d != nullptr && baseComplex != nullptr){
		Eigen::MatrixXd m = cg3::rotationMatrix(Vec3d(ui->xvSpinBox->value(),ui->yvSpinBox->value(),ui->zvSpinBox->value()), (ui->angleSpinBox->value()*M_PI)/180);
        he->rotate(m);
        markerMesh.rotate(m);
        d->rotate(m);
        d->updateFaceNormals();
        d->updateVertexNormals();
        d->updateBoundingBox();
        originalMesh.rotate(m);
        originalMesh.updateFacesAndVerticesNormals();
        baseComplex->rotate(m);
        mainWindow.canvas.update();
    }
}

void EngineManager::on_saveCurrentBlockPushButton_clicked() {
    if (he != nullptr){
        he->getHeightfield(ui->heightfieldsSlider->value()).saveOnObj("Block.obj");
    }
}

void EngineManager::on_tinyFeaturesPushButton_clicked() {
    if (d != nullptr && he != nullptr && solutions != nullptr){
        struct op{
                bool operator()(const std::pair<double, unsigned int>& p1, const std::pair<double, unsigned int>& p2){
                    return p1.second < p2.second;
                }
        };
        for (unsigned int i = 0; i < he->getNumHeightfields(); i++){
            libigl::removeDuplicateVertices(he->getHeightfield(i));
        }
        priorityBoxes.clear();
        std::vector<std::pair<double, unsigned int> > tinyPieces;
        bool inserted = false;
        unsigned int i = 1;
        do {
            tinyPieces.clear();
            for (unsigned int i = 0; i < he->getNumHeightfields(); i++){
                double mindist;
				if (TinyFeatureDetection::tinyFeaturePlane(he->getHeightfield(i),he->getTarget(i), d->averageHalfEdgesLength()*ui->thresholdTinyFeaturesSpinBox->value(), mindist)){
                    tinyPieces.push_back(std::pair<double, unsigned int>(mindist, solutions->getBox(i).getId()));
                    //he->getHeightfield(i).setFaceColor(Color(255,0,0));
                }
            }

            std::cerr << "Tiny Pieces:\n";
            for (std::pair<double, unsigned int> p :tinyPieces){
                std::cerr << p.second << "; ";
            }
            std::cerr << "\n";

            std::sort(tinyPieces.begin(), tinyPieces.end(), op());

            inserted = false;
            for (unsigned int i = 0; i < tinyPieces.size() && !inserted; i++){
                std::pair<double, unsigned int> p = tinyPieces[i];
                std::list<unsigned int>::iterator it = std::find(priorityBoxes.begin(), priorityBoxes.end(), splittedBoxesToOriginals.at(p.second));
                if (it == priorityBoxes.end()){
                    priorityBoxes.push_front(splittedBoxesToOriginals.at(p.second));
                    std::string s = ui->listLabel->text().toStdString();
                    ui->listLabel->setText(QString::fromStdString(std::to_string(splittedBoxesToOriginals.at(p.second)) + "; " + s));
                    inserted = true;
                }
            }
            if (!inserted && tinyPieces.size() > 0)
                QMessageBox::warning(this, "Tiny Pieces", "It is not possible to avoid Tiny Pieces on this decomposition.");
            else if (inserted) {
                on_restoreBoxesPushButton_clicked();
                on_reorderBoxes_clicked();
                on_subtractPushButton_clicked();
                on_colorPiecesPushButton_clicked();
				serializeBC(hfdls.actualPath() + "/bools" + std::to_string(i) + ".hfd");
                i++;
            }
        } while (inserted);
    }
    mainWindow.canvas.update();
}

void EngineManager::on_mergePushButton_clicked() {
    if (he != nullptr && solutions != nullptr && d != nullptr){
        Engine::mergePostProcessing(*he, *solutions, *baseComplex, *d, ui->mergeDownwardsCheckBox->isChecked());
        ui->solutionsSlider->setMaximum(solutions->getNumberBoxes()-1);
        ui->heightfieldsSlider->setMaximum(solutions->getNumberBoxes()-1);
        mainWindow.canvas.update();
    }
}

void EngineManager::on_finalizePushButton_clicked() {
    if (he != nullptr){
        on_markerMeshPushButton_clicked();
        for (unsigned int h = 0; h < he->getNumHeightfields(); h++){
            EigenMesh& m = he->getHeightfield(h);

            //normals
			for (unsigned int f = 0; f < m.numberFaces(); f++){
                bool found = false;
                for (unsigned int i = 0; i < 6 && !found; i++) {
					if (epsilonEqual(m.faceNormal(f), AXIS[i]))
                        found = true;
                }
                if (found){
					Point3i face = m.face(f);
					m.addVertex(m.vertex(face.x()));
					m.addVertex(m.vertex(face.y()));
					m.addVertex(m.vertex(face.z()));
					unsigned int v3 = m.numberVertices()-1;
                    unsigned int v2 = v3-1;
                    unsigned int v1 = v2-1;
					m.setVertexNormal(m.faceNormal(f), v1);
					m.setVertexNormal(m.faceNormal(f), v2);
					m.setVertexNormal(m.faceNormal(f), v3);
                    m.setFace(f, v1, v2, v3);
                }
            }
        }
    }
}
