CONFIG(debug, debug|release){
    DEFINES += DEBUG
}
CONFIG(release, debug|release){
    DEFINES -= DEBUG
    #just uncomment next lines if you want to ignore asserts and got a more optimized binary
    CONFIG += FINAL_RELEASE
}

#CONFIG += c++14

DESTDIR = release

CONFIG += ALL
CONFIG += SERVER_MODE

ALL {
    CONFIG += CG3_CORE CG3_DATA_STRUCTURES CG3_MESHES CG3_ALGORITHMS CG3_CGAL CG3_CINOLIB CG3_LIBIGL CG3_VIEWER
    include(cg3lib/cg3.pri)
}

SERVER_MODE {
    DEFINES += SERVER_MODE
    CONFIG += FINAL_RELEASE
    message(Server mode!)
}
!SERVER_MODE {
    message(GUI mode)
}

message(Included modules: $$MODULES)
FINAL_RELEASE {
    message(Final Release!)
}

FINAL_RELEASE {
    unix:!macx{
        QMAKE_CXXFLAGS_RELEASE -= -g -O2
        QMAKE_CXXFLAGS += -O3 -DNDEBUG
    }
}

exists($$(GUROBI_HOME)){
    message (Gurobi)
    INCLUDEPATH += $$(GUROBI_HOME)/include
    LIBS += -L$$(GUROBI_HOME)/lib -lgurobi_g++5.2 -lgurobi90
    DEFINES += GUROBI_DEFINED
}

HEADERS += \
    common.h \
    GUI/managers/enginemanager.h \
    engine/tricubic.h \
    engine/energy.h \
    engine/box.h \
    engine/boxlist.h \
    engine/engine.h \
    engine/heightfieldslist.h \
    engine/packing.h \
    engine/splitting.h \
    engine/reconstruction.h \
    engine/unsigned_distances.h \
    lib/grid/grid.h \
    lib/packing/binpack2d.h \
    lib/graph/undirectednode.h \
    lib/graph/directedgraph.h \
    engine/tinyfeaturedetection.h

SOURCES += \
    engine/unsigned_distances.cpp \
    main.cpp \
    common.cpp \
    GUI/managers/enginemanager.cpp \
    engine/tricubic.cpp \
    engine/energy.cpp \
    engine/box.cpp \
    engine/boxlist.cpp \
    engine/engine.cpp \
    engine/heightfieldslist.cpp \
    engine/packing.cpp \
    engine/splitting.cpp \
    engine/reconstruction.cpp \
    lib/grid/grid.cpp \
    lib/grid/drawablegrid.cpp \
    engine/tinyfeaturedetection.cpp \
    engine/tinyfeaturedetection2.cpp

FORMS += \
    GUI/managers/enginemanager.ui

DISTFILES += \
    README.md
