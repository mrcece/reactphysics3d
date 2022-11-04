/********************************************************************************
* ReactPhysics3D physics library, http://www.reactphysics3d.com                 *
* Copyright (c) 2010-2022 Daniel Chappuis                                       *
*********************************************************************************
*                                                                               *
* This software is provided 'as-is', without any express or implied warranty.   *
* In no event will the authors be held liable for any damages arising from the  *
* use of this software.                                                         *
*                                                                               *
* Permission is granted to anyone to use this software for any purpose,         *
* including commercial applications, and to alter it and redistribute it        *
* freely, subject to the following restrictions:                                *
*                                                                               *
* 1. The origin of this software must not be misrepresented; you must not claim *
*    that you wrote the original software. If you use this software in a        *
*    product, an acknowledgment in the product documentation would be           *
*    appreciated but is not required.                                           *
*                                                                               *
* 2. Altered source versions must be plainly marked as such, and must not be    *
*    misrepresented as being the original software.                             *
*                                                                               *
* 3. This notice may not be removed or altered from any source distribution.    *
*                                                                               *
********************************************************************************/

// Libraries
#include <reactphysics3d/collision/ConvexMesh.h>
#include <reactphysics3d/memory/MemoryManager.h>
#include <reactphysics3d/collision/PolygonVertexArray.h>
#include <reactphysics3d/utils/DefaultLogger.h>
#include <reactphysics3d/engine/PhysicsCommon.h>
#include <reactphysics3d/utils/Error.h>
#include <cstdlib>

using namespace reactphysics3d;


// Constructor
/**
 * Create a convex mesh given an array of polygons.
 * @param polygonVertexArray Pointer to the array of polygons and their vertices
 */
ConvexMesh::ConvexMesh(MemoryAllocator& allocator)
               : mMemoryAllocator(allocator), mHalfEdgeStructure(allocator, 6, 8, 24),
                 mVertices(allocator), mFacesNormals(allocator) {

}

/// Static factory method to create a convex mesh. This methods returns null_ptr if the mesh is not valid
ConvexMesh* ConvexMesh::create(MemoryAllocator& allocator) {

    ConvexMesh* mesh = new (allocator.allocate(sizeof(ConvexMesh))) ConvexMesh(allocator);
    return mesh;
}

// Initialize a mesh and returns errors if any
bool ConvexMesh::init(const PolygonVertexArray& polygonVertexArray, std::vector<Error>& errors) {

    bool isValid = true;

    // Reserve memory for the vertices, faces and edges
    mVertices.reserve(polygonVertexArray.getNbVertices());
    mFacesNormals.reserve(polygonVertexArray.getNbFaces());
    mHalfEdgeStructure.reserve(polygonVertexArray.getNbFaces(), polygonVertexArray.getNbVertices(),
                               (polygonVertexArray.getNbVertices() + polygonVertexArray.getNbFaces() - 2) * 2);

    // Copy the vertices from the PolygonVertexArray into the mesh
    isValid &= copyVertices(polygonVertexArray, errors);

    // Create the half-edge structure of the mesh
    isValid &= createHalfEdgeStructure(polygonVertexArray, errors);

    // Compute the faces normals
    isValid &= computeFacesNormals(errors);

   return isValid;
}

// Copy the vertices into the mesh
bool ConvexMesh::copyVertices(const PolygonVertexArray& polygonVertexArray, std::vector<Error>& errors) {

    bool isValid = true;

    mCentroid.setToZero();

    for (uint32 i=0 ; i < polygonVertexArray.getNbVertices(); i++) {

        const Vector3 vertex = polygonVertexArray.getVertex(i);
        mVertices.add(vertex);
        mCentroid += vertex;
    }

    if (getNbVertices() > 0) {

        mCentroid /= static_cast<decimal>(getNbVertices());
    }
    else {

        errors.push_back(Error("The mesh does not have any vertices"));

        isValid = false;
    }

    return isValid;
}

// Create the half-edge structure of the mesh
/// This method returns true if the mesh is valid or false otherwise
bool ConvexMesh::createHalfEdgeStructure(const PolygonVertexArray& polygonVertexArray, std::vector<Error>& errors) {

    bool isValid = true;

    // For each vertex of the mesh
    const uint32 nbVertices = polygonVertexArray.getNbVertices();
    for (uint32 v=0; v < nbVertices; v++) {
        mHalfEdgeStructure.addVertex(v);
    }

    uint32 nbEdges = 0;

    // For each polygon face of the mesh
    const uint32 nbFaces = polygonVertexArray.getNbFaces();
    for (uint32 f=0; f < nbFaces; f++) {

        // Get the polygon face
        PolygonVertexArray::PolygonFace* face = polygonVertexArray.getPolygonFace(f);

        Array<uint32> faceVertices(mMemoryAllocator, face->nbVertices);

        // For each vertex of the face
        for (uint32 v=0; v < face->nbVertices; v++) {
            faceVertices.add(polygonVertexArray.getVertexIndexInFace(f, v));
        }

        nbEdges += face->nbVertices;

        // If the face has less than three vertices
        if (faceVertices.size() < 3) {

            // Create a new error
            errors.push_back(Error(std::string("The face with index " + std::to_string(f) + " has less than three vertices")));

            isValid = false;

            RP3D_LOG("PhysicsCommon", Logger::Level::Error, Logger::Category::PhysicCommon,
                     "Error when creating a ConvexMesh: Mesh has a face with less than three vertices.",  __FILE__, __LINE__);
        }

        // Addd the face into the half-edge structure
        mHalfEdgeStructure.addFace(faceVertices);
    }

    nbEdges /= 2;

    // If the mesh is not valid (check Euler formula V + F - E = 2) (has duplicated vertices)
    if (2 + nbEdges - polygonVertexArray.getNbFaces() != polygonVertexArray.getNbVertices()) {

        RP3D_LOG("PhysicsCommon", Logger::Level::Error, Logger::Category::PhysicCommon,
                 "Error when creating a ConvexMesh: input PolygonVertexArray is not valid. Mesh with duplicated vertices is not supported.",  __FILE__, __LINE__);

        // Create a new error
        errors.push_back(Error(std::string("Convex Mesh might have duplicated vertices (invalid Euler formula)")));

        isValid = false;
    }

    // Initialize the half-edge structure
    mHalfEdgeStructure.computeHalfEdges();

    return isValid;
}

// Compute the faces normals
bool ConvexMesh::computeFacesNormals(std::vector<Error>& errors) {

    bool isValid = true;

    mFacesNormals.clear();

    // For each face
    const uint32 nbFaces = mHalfEdgeStructure.getNbFaces();
    for (uint32 f=0; f < nbFaces; f++) {

        const HalfEdgeStructure::Face& face = mHalfEdgeStructure.getFace(f);

        if (face.faceVertices.size() >= 3) {

            const Vector3 vec1 = getVertex(face.faceVertices[1]) - getVertex(face.faceVertices[0]);
            const Vector3 vec2 = getVertex(face.faceVertices[2]) - getVertex(face.faceVertices[0]);
            mFacesNormals.add(vec1.cross(vec2));

            if (mFacesNormals[f].lengthSquare() > MACHINE_EPSILON) {

                mFacesNormals[f].normalize();
            }
            else {
               isValid = false;
               errors.push_back(Error("Face with index " + std::to_string(f) + " has a zero area"));
            }
        }
    }

    return isValid;
}

// Compute and return the area of a face
decimal ConvexMesh::getFaceArea(uint32 faceIndex) const {

    Vector3 sumCrossProducts(0, 0, 0);

    const HalfEdgeStructure::Face& face = mHalfEdgeStructure.getFace(faceIndex);
    assert(face.faceVertices.size() >= 3);

    const Vector3& v1 = getVertex(face.faceVertices[0]);

    // For each vertex of the face
    const uint32 nbFaceVertices = static_cast<uint32>(face.faceVertices.size());
    for (uint32 i=2; i < nbFaceVertices; i++) {

        const Vector3& v2 = getVertex(face.faceVertices[i-1]);
        const Vector3& v3 = getVertex(face.faceVertices[i]);

        const Vector3 v1v2 = v2 - v1;
        const Vector3 v1v3 = v3 - v1;

        sumCrossProducts +=  v1v2.cross(v1v3);
    }

    return decimal(0.5) * sumCrossProducts.length();
}

// Compute and return the volume of the convex mesh
/// We use the divergence theorem to compute the volume of the convex mesh using a sum over its faces.
decimal ConvexMesh::getVolume() const {

    decimal sum = 0.0;

    // For each face of the mesh
    for (uint32 f=0; f < getNbFaces(); f++) {

        const HalfEdgeStructure::Face& face = mHalfEdgeStructure.getFace(f);
        const decimal faceArea = getFaceArea(f);
        const Vector3 faceNormal = mFacesNormals[f];
        const Vector3& faceVertex = getVertex(face.faceVertices[0]);

        sum += faceVertex.dot(faceNormal) * faceArea;
    }

    return std::abs(sum) / decimal(3.0);
}
