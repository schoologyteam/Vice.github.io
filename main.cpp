#include <string.h>
#include <stdio.h>
#include <iostream>
#include <math.h>
#include <stdlib.h>
#include <assert.h>

#include <windows.h>

#include "renderware.h"
#include "loaders/Clump.h"
#include "loaders/IMG.hpp"
#include "loaders/IPL.hpp"
#include "loaders/IDE.hpp"

#include <osgViewer/Viewer>
#include <osg/Material>
#include <osg/Texture2D>
#include <osg/MatrixTransform>
#include <osg/ShapeDrawable>
#include <osg/Geometry>
#include <osg/Geode>
#include <osg/PolygonMode>
#include <osgGA/TrackballManipulator>
#include <osgGA/FirstPersonManipulator>
#include <osgGA/FlightManipulator>
#include <osgGA/DriveManipulator>
#include <osgGA/GUIEventHandler>
#include <osg/Timer>
#include <osgText/Text>

#define PROJECT_NAME "openvice"
#define WINDOW_WIDTH 1920
#define WINDOW_HEIGHT 1080
#define WINDOW_TITLE L"openvice"

struct GameMaterial {
	char name[MAX_LENGTH_FILENAME]; /* without extension ".TXD" */
	bool isAlpha;
	osg::ref_ptr<osg::Image> image;
};

std::vector<IDE*> g_ideFile;
std::vector<IPL*> g_ipl;
std::vector<GameMaterial> g_Textures;

template <typename T>
void remove_duplicates(std::vector<T>& vec)
{
	std::sort(vec.begin(), vec.end());
	vec.erase(std::unique(vec.begin(), vec.end()), vec.end());
}

void loadTextures(char *bytes)
{
	size_t offset = 0;

	TextureDictionary txd;
	txd.read(bytes, &offset);

	/* Loop for every texture in TXD file */
	for (uint32_t i = 0; i < txd.texList.size(); i++) {
		NativeTexture& t = txd.texList[i];
		// printf("%s %s %d %d %d %d\n", t.name, t.maskName.c_str(), t.width[0], t.height[0], t.depth, t.rasterFormat);

		uint8_t* texelsToArray = t.texels[0];
		size_t len = t.dataSizes[0];

		struct GameMaterial m;
		memcpy(m.name, t.name, sizeof(t.name)); /* without extension ".TXD" */

		m.isAlpha = t.isAlpha;

		GLenum format = GL_COMPRESSED_RGB_S3TC_DXT1_EXT;

		switch (t.dxtCompression)
		{
		default:
		case 1: /* DXT1 */
			format = GL_COMPRESSED_RGB_S3TC_DXT1_EXT;
			if (t.isAlpha) {
				format = GL_COMPRESSED_RGBA_S3TC_DXT1_EXT;
			}
			break;
		case 3: /* DXT3 */
			format = GL_COMPRESSED_RGBA_S3TC_DXT3_EXT;
			break;
		case 5: /* DXT5 */
			format = GL_COMPRESSED_RGBA_S3TC_DXT5_EXT;
			break;
		}

		osg::Image* image = new osg::Image();
		image->setFileName(t.name);
		image->allocateImage(t.width[0], t.height[0], t.depth, format, GL_UNSIGNED_BYTE);

		uint8_t* data = reinterpret_cast<uint8_t*>(image->data());
		memcpy(data, t.texels[0], len);
		//image->dirty();

		m.image = image;

		g_Textures.push_back(m);
	}
}

int LoadFileTXD(const char* filename)
{
	printf("[Info] Loading file: %s\n", filename);

	FILE* fp;
	long size;
	char* buffer;

	fp = fopen(filename, "rb");
	if (!fp) {
		perror(filename);
	}

	fseek(fp, 0L, SEEK_END);
	size = ftell(fp);
	rewind(fp);

	buffer = (char*)calloc(1, size + 1);
	if (!buffer) {
		fclose(fp);
		fputs("memory alloc fails", stderr);
		return 1;
	}

	/* copy the file into the buffer */
	if (1 != fread(buffer, size, 1, fp)) {
		fclose(fp);
		free(buffer);
		fputs("entire read fails", stderr);
		return 1;
	}

	fclose(fp);

	loadTextures(buffer);

	free(buffer);

	return 0;
}

int LoadFileTXD(IMG *pImgLoader, const char *filename)
{
	char result_name[MAX_LENGTH_FILENAME + 4];
	strcpy(result_name, filename);
	strcat(result_name, ".txd");

	int fileId = pImgLoader->GetFileIndexByName(result_name);
	if (fileId == -1) {
		printf("[Error] Cannot find file %s in IMG archive\n", result_name);
		return 1;
	}

	char *fileBuffer = (char*)pImgLoader->GetFileById(fileId);

	loadTextures(fileBuffer);

	return 0;
}

void checkOpenGLError(const std::string& functionName)
{
	GLenum error = glGetError();
	while (error != GL_NO_ERROR) {
		switch (error) {
		case GL_INVALID_ENUM:
			std::cerr << "OpenGL Error in " << functionName << ": GL_INVALID_ENUM" << std::endl;
			break;
		case GL_INVALID_VALUE:
			std::cerr << "OpenGL Error in " << functionName << ": GL_INVALID_VALUE" << std::endl;
			break;
		case GL_INVALID_OPERATION:
			std::cerr << "OpenGL Error in " << functionName << ": GL_INVALID_OPERATION" << std::endl;
			break;
		case GL_OUT_OF_MEMORY:
			std::cerr << "OpenGL Error in " << functionName << ": GL_OUT_OF_MEMORY" << std::endl;
			break;
		default:
			std::cerr << "OpenGL Error in " << functionName << ": Unknown error " << error << std::endl;
			break;
		}
		error = glGetError(); // Check for more errors
	}
}

osg::ref_ptr<osg::Group> loadDFF(IMG* imgLoader, char *name)
{
	// checkOpenGLError("loadDFF");

	/* Skip LOD files */
	if (strstr(name, "LOD") != NULL) {
		return NULL;
	}

	if (strstr(name, "lod") != NULL) {
		return NULL;
	}

	char result_name[MAX_LENGTH_FILENAME + 4];
	strcpy(result_name, name);
	strcat(result_name, ".dff");

	int fileId = imgLoader->GetFileIndexByName(result_name);
	if (fileId == -1) {
		return NULL;
	}

	char* fileBuffer = (char*)imgLoader->GetFileById(fileId);

	osg::ref_ptr<osg::Group> root = new osg::Group;


	Clump* clump = new Clump();
	clump->Read(fileBuffer);

	for (uint32_t index = 0; index < clump->m_numGeometries; index++) {
		
		osg::ref_ptr<osg::Geode> geode = new osg::Geode;
		std::vector<std::string> modelTextures;

		/* Load all materials */
		uint32_t materials = clump->GetGeometryList()[index]->m_numMaterials;

		for (uint32_t i = 0; i < materials; i++) {
			Material* material = clump->GetGeometryList()[index]->materialList[i];
			modelTextures.push_back(material->texture.name);
		}

		/*for (uint32_t i = 0; i < clump->GetGeometryList()[index].vertexCount; i++) {

			// Load texture coordinates model
			if (clump->GetGeometryList()[index].flags & FLAGS_TEXTURED
				// || clump->GetGeometryList()[index].flags & FLAGS_TEXTURED2
				) {
				for (uint32_t j = 0; j < 1 / clump->GetGeometryList()[index].numUVs /; j++) { / insert now FLAGS_TEXTURED /

					float tx = clump->GetGeometryList()[index].texCoords[j][i * 2 + 0];
					float ty = clump->GetGeometryList()[index].texCoords[j][i * 2 + 1];

					modelTextureCoord.push_back(tx);
					modelTextureCoord.push_back(ty);
				}
			}
		}*/



		/* Loop for every mesh */
		for (uint32_t i = 0; i < clump->GetGeometryList()[index]->splits.size(); i++) {

			osg::ref_ptr<osg::Geometry> geometry = new osg::Geometry;

			osg::ref_ptr<osg::Vec3Array> vertices = new osg::Vec3Array;
			osg::ref_ptr<osg::Vec2Array> texcoords = new osg::Vec2Array;
			osg::ref_ptr<osg::Vec3Array> normals = new osg::Vec3Array;
			osg::ref_ptr<osg::Vec4Array> colors = new osg::Vec4Array;

			int countVertices = clump->GetGeometryList()[index]->vertexCount;

			for (int v = 0; v < countVertices; v++) {
				float x = clump->GetGeometryList()[index]->vertices[v * 3 + 0];
				float y = clump->GetGeometryList()[index]->vertices[v * 3 + 1];
				float z = clump->GetGeometryList()[index]->vertices[v * 3 + 2];

				vertices->push_back(osg::Vec3(x, y, z));

				if (clump->GetGeometryList()[index]->flags & FLAGS_NORMALS) {
					float nx = clump->GetGeometryList()[index]->normals[v * 3 + 0];
					float ny = clump->GetGeometryList()[index]->normals[v * 3 + 1];
					float nz = clump->GetGeometryList()[index]->normals[v * 3 + 2];

					normals->push_back(osg::Vec3(nx, ny, nz));
				}

				float tx = 0.0f;
				float ty = 0.0f;

				if (clump->GetGeometryList()[index]->flags & FLAGS_TEXTURED) {
					tx = clump->GetGeometryList()[index]->texCoords[0][v * 2 + 0];
					ty = clump->GetGeometryList()[index]->texCoords[0][v * 2 + 1];
				}
				texcoords->push_back(osg::Vec2(tx, ty));


				uint8_t cx = 0;
				uint8_t cy = 0;
				uint8_t cz = 0;
				uint8_t cr = 0;
				if (clump->GetGeometryList()[index]->flags & FLAGS_PRELIT) {
					cx = clump->GetGeometryList()[index]->vertexColors[v * 4 + 0];
					cy = clump->GetGeometryList()[index]->vertexColors[v * 4 + 1];
					cz = clump->GetGeometryList()[index]->vertexColors[v * 4 + 2];
					cr = clump->GetGeometryList()[index]->vertexColors[v * 4 + 3];
				}
				colors->push_back(osg::Vec4(1.0, 1.0, 1.0, 1.0));
			}

			osg::ref_ptr<osg::DrawElementsUInt> indices = new osg::DrawElementsUInt(
				clump->GetGeometryList()[index]->faceType == FACETYPE_STRIP
				? osg::PrimitiveSet::TRIANGLE_STRIP // D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP
				: osg::PrimitiveSet::TRIANGLES // D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST
				,
				clump->GetGeometryList()[index]->splits[i].m_numIndices
			);

			memcpy(
				(void*)indices->getDataPointer(), 
				(unsigned int*)clump->GetGeometryList()[index]->splits[i].indices,
				sizeof(uint32_t) * clump->GetGeometryList()[index]->splits[i].m_numIndices
			);

			geometry->setVertexArray(vertices.get());

			if (clump->GetGeometryList()[index]->flags & FLAGS_TEXTURED) {
				geometry->setTexCoordArray(0, texcoords.get());
			}

			if (clump->GetGeometryList()[index]->flags & FLAGS_NORMALS) {
				geometry->setNormalArray(normals.get());
				geometry->setNormalBinding(osg::Geometry::BIND_OVERALL);
			}

			geometry->setColorArray(colors.get());
			geometry->setColorBinding(osg::Geometry::BIND_PER_VERTEX);

			geometry->addPrimitiveSet(indices.get());


			// Wireframe
			// osg::ref_ptr<osg::PolygonMode> pm = new osg::PolygonMode;
			// pm->setMode(osg::PolygonMode::FRONT_AND_BACK, osg::PolygonMode::LINE);
			// geometry->getOrCreateStateSet()->setAttribute(pm.get());


			uint32_t materialIndex = clump->GetGeometryList()[index]->splits[i].matIndex;


			int matIndex = -1;

			// Find texture
			for (int im = 0; im < g_Textures.size(); im++) {
				if (strcmp(g_Textures[im].name, modelTextures[materialIndex].c_str()) == 0) {
					matIndex = im;
					break;
				}
			}

			if (matIndex != -1) {

				osg::ref_ptr<osg::Texture2D> texture = new osg::Texture2D;
				texture->setImage(g_Textures[matIndex].image);

				// Set texture wrapping modes
				texture->setWrap(osg::Texture::WRAP_S, osg::Texture::WrapMode::REPEAT); // Wrap horizontally
				texture->setWrap(osg::Texture::WRAP_T, osg::Texture::WrapMode::REPEAT); // Wrap vertically
				//texture->setWrap(osg::Texture::WRAP_R, osg::Texture::WrapMode::REPEAT);

				//texture->setFilter(osg::Texture::MIN_FILTER, osg::Texture::LINEAR);
				//texture->setFilter(osg::Texture::MAG_FILTER, osg::Texture::LINEAR);

				geometry->getOrCreateStateSet()->setTextureAttributeAndModes(0, texture);

				if (g_Textures[matIndex].isAlpha) {
					geometry->getStateSet()->setMode(GL_BLEND, osg::StateAttribute::ON);
					geometry->getStateSet()->setRenderingHint(osg::StateSet::TRANSPARENT_BIN);
				}
			}
			
			geode->addDrawable(geometry.get());

		}

		root->addChild(geode.get());
	}

	clump->Clear();
	delete clump;

	return root;
}

osg::ref_ptr<osg::Geometry> createPlane(float width, float height)
{
	osg::ref_ptr<osg::Geometry> geometry = new osg::Geometry;

	// Define the vertices of the plane (4 vertices, one for each corner)
	osg::ref_ptr<osg::Vec3Array> vertices = new osg::Vec3Array;
	vertices->push_back(osg::Vec3(-width / 2.0f, -height / 2.0f, 0.0f)); // Bottom-left
	vertices->push_back(osg::Vec3(width / 2.0f, -height / 2.0f, 0.0f));  // Bottom-right
	vertices->push_back(osg::Vec3(width / 2.0f, height / 2.0f, 0.0f));   // Top-right
	vertices->push_back(osg::Vec3(-width / 2.0f, height / 2.0f, 0.0f));  // Top-left

	geometry->setVertexArray(vertices);

	// Define the color for the plane (optional)
	osg::ref_ptr<osg::Vec4Array> colors = new osg::Vec4Array;
	colors->push_back(osg::Vec4(0.5f, 0.5f, 0.5f, 1.0f)); // Gray color
	geometry->setColorArray(colors, osg::Array::BIND_OVERALL);

	osg::ref_ptr<osg::Vec2Array> texCoords = new osg::Vec2Array;
	texCoords->push_back(osg::Vec2(0.0f, 0.0f)); // Bottom-left
	texCoords->push_back(osg::Vec2(100.0f, 0.0f)); // Bottom-right
	texCoords->push_back(osg::Vec2(100.0f, 100.0f)); // Top-right
	texCoords->push_back(osg::Vec2(0.0f, 100.0f)); // Top-left

	geometry->setTexCoordArray(0, texCoords.get());

	// Define the face (triangle) using the vertex indices
	osg::ref_ptr<osg::DrawElementsUInt> indices = new osg::DrawElementsUInt(osg::PrimitiveSet::TRIANGLES, 0);
	
	// First triangle (Bottom-left, Bottom-right, Top-right)
	indices->push_back(0); // Bottom-left
	indices->push_back(1); // Bottom-right
	indices->push_back(2); // Top-right

	// Second triangle (Top-left, Bottom-left, Top-right)
	indices->push_back(3); // Top-left
	indices->push_back(0); // Bottom-left
	indices->push_back(2); // Top-right

	geometry->addPrimitiveSet(indices);

	return geometry;
}

osg::ref_ptr<osg::MatrixTransform> createWater()
{
	osg::ref_ptr<osg::Geometry> plane = createPlane(5000.0f, 5000.0f);
	osg::ref_ptr<osg::Geode> geode = new osg::Geode;
	
	osg::ref_ptr<osg::MatrixTransform> transform = new osg::MatrixTransform();

	osg::Matrix pos;
	pos.setTrans(osg::Vec3(0.0f, 0.0f, 5.0f));

	transform->setMatrix(pos);

	osg::ref_ptr<osg::Texture2D> texture = new osg::Texture2D;
	for (int g = 0; g < g_Textures.size(); g++) {
		if (strcmp(g_Textures[g].name, "waterclear256") == 0) {
			texture->setImage(g_Textures[g].image);
			break;
		}
	}

	// Set texture wrapping modes
	texture->setWrap(osg::Texture::WRAP_S, osg::Texture::WrapMode::REPEAT); // Wrap horizontally
	texture->setWrap(osg::Texture::WRAP_T, osg::Texture::WrapMode::REPEAT); // Wrap vertically

	plane->getOrCreateStateSet()->setTextureAttributeAndModes(0, texture);

	geode->addDrawable(plane);
	transform->addChild(geode);

	return transform;
}

class MyCameraManipulator : public osgGA::FirstPersonManipulator
{
public:
	MyCameraManipulator()
	{
		// Set initial camera position
		_cameraPos = osg::Vec3(0.0f, -10.0f, 5.0f);
		_cameraRot = osg::Quat(0.0f, osg::Vec3(0.0f, 0.0f, 1.0f)); // No rotation at the beginning
	}

	virtual void handleKeyEvent(const osgGA::GUIEventAdapter& ea, osgGA::GUIActionAdapter& us)
	{
		switch (ea.getEventType())
		{
		case osgGA::GUIEventAdapter::KEYDOWN:
			if (ea.getKey() == 'w') {
				_cameraPos += osg::Vec3(0.0f, 0.1f, 0.0f);  // Move forward (along negative Y-axis)
			}
			else if (ea.getKey() == 's') {
				_cameraPos -= osg::Vec3(0.0f, 0.1f, 0.0f);  // Move backward (along positive Y-axis)
			}
			else if (ea.getKey() == 'a') {
				_cameraPos -= osg::Vec3(0.1f, 0.0f, 0.0f);  // Move left (along negative X-axis)
			}
			else if (ea.getKey() == 'd') {
				_cameraPos += osg::Vec3(0.1f, 0.0f, 0.0f);  // Move right (along positive X-axis)
			}
			else if (ea.getKey() == 'q') {
				_cameraRot *= osg::Quat(0.1f, osg::Vec3(0.0f, 0.0f, 1.0f));  // Rotate left (yaw)
			}
			else if (ea.getKey() == 'e') {
				_cameraRot *= osg::Quat(-0.1f, osg::Vec3(0.0f, 0.0f, 1.0f));  // Rotate right (yaw)
			}
			break;
		default:
			break;
		}
		updateCamera();
	}

	void updateCamera()
	{
		// Apply position and rotation transformations
		osg::MatrixTransform* transform = new osg::MatrixTransform;
		transform->setMatrix(osg::Matrix::translate(_cameraPos) * osg::Matrix::rotate(_cameraRot));

		// Set this transformation on the viewer's camera
		viewer->getCamera()->setViewMatrix(transform->getMatrix());
	}

	void setViewer(osgViewer::Viewer* _viewer)
	{
		viewer = _viewer;
	}

	osg::Vec3 _cameraPos;
	osg::Quat _cameraRot;
	osgViewer::Viewer* viewer;
};

class CustomFirstPersonManipulator : public osgGA::FirstPersonManipulator
{
public:
	float speed = 1;

	CustomFirstPersonManipulator() : osgGA::FirstPersonManipulator()
	{
	
	}

	virtual bool handle(const osgGA::GUIEventAdapter& ea, osgGA::GUIActionAdapter& us) override
	{
		osgViewer::Viewer* viewer = dynamic_cast<osgViewer::Viewer*>(&us);
		if (!viewer) return false;

		//if (ea.getKey() == ea.KEY_Shift_L) {
		//	speed = 10;
		//}

		switch (ea.getEventType()) {
		/*case osgGA::GUIEventAdapter::KEYUP:
			switch (ea.getKey()) {
			case ea.KEY_Shift_L:
				speed = 1;
			}*/
			case osgGA::GUIEventAdapter::KEYDOWN:
			{
				printf("key = %d hex = 0x%08x \n", ea.getKey(), ea.getKey());
				if (ea.getKey() == 87) {
					speed = 10.0;  // Speed up when SHIFT is held
					moveForward();
					printf("87 \n");
				}

				if (ea.getKey() == (osgGA::GUIEventAdapter::MODKEY_SHIFT | ea.KEY_W)) {
					speed = 10.0;  // Speed up when SHIFT is held
					moveForward();
					printf("| \n");
				}

				if (ea.getKey() == (osgGA::GUIEventAdapter::MODKEY_SHIFT && ea.KEY_W)) {
					speed = 10.0;  // Speed up when SHIFT is held
					moveForward();
					printf("&& \n");
				}
				if (ea.getKey() == (osgGA::GUIEventAdapter::MODKEY_SHIFT || ea.KEY_W)) {
					speed = 10.0;  // Speed up when SHIFT is held
					moveForward();
					printf("|| \n");
				}
				if (ea.getKey() == (osgGA::GUIEventAdapter::MODKEY_SHIFT)) {
					speed = 10.0;  // Speed up when SHIFT is held
					moveForward();
					printf("MODKEY_SHIFT \n");
				}
				if (ea.getKey() == (osgGA::GUIEventAdapter::MODKEY_SHIFT & ea.KEY_W)) {
					speed = 10.0;  // Speed up when SHIFT is held
					moveForward();
					printf("MODKEY_SHIFT & ea.KEY_W \n");
				}
				if (ea.getKey() == (osgGA::GUIEventAdapter::KEY_Shift_L)) {
					speed = 10.0;  // Speed up when SHIFT is held
					moveForward();
					printf("KEY_Shift_L \n");
				}
				if (ea.getKey() == ea.KEY_W) {
					moveForward();
					printf("ea.KEY_W \n");
				}
				if (ea.getKey() == ea.KEY_S) {
					moveBackward();
				}				
				break;
			}
			case osgGA::GUIEventAdapter::KEYUP:
			{
				break;
			}
		}

		return osgGA::FirstPersonManipulator::handle(ea, us);
	}

	void setViewer(osgViewer::Viewer* _viewer)
	{
		viewer = _viewer;
	}

	osgViewer::Viewer* viewer;

private:
	osg::Vec3f eye = osg::Vec3f(0.0, -200.0, 0.0);
	osg::Vec3f centre = osg::Vec3f(0.0, 0.0, 0.0);
	osg::Vec3f up = osg::Vec3f(0.0, 0.0, 1.0);

	void moveForward()
	{
		osg::Matrix view = viewer->getCamera()->getViewMatrix();
		view.getLookAt(eye, centre, up);

		osg::Vec3f actuallook = centre - eye;

		actuallook = actuallook / (actuallook.length());

		eye = eye + ((actuallook) * speed);
		centre = centre + ((actuallook) * speed);

		setTransformation(eye, centre, osg::Z_AXIS);

		speed = 1;
	}

	void moveBackward() {
		osg::Matrix view = viewer->getCamera()->getViewMatrix();
		view.getLookAt(eye, centre, up);

		osg::Vec3f actuallook = centre - eye;

		actuallook = actuallook / (actuallook.length());

		eye = eye + ((actuallook) * -speed);
		centre = centre + ((actuallook) * -speed);

		setTransformation(eye, centre, osg::Z_AXIS);

		speed = 1;
	}

	void moveLeft() {
		//osg::Vec3d direction = getViewMatrixAsLookAt().getLookAt() - getViewMatrixAsLookAt().getEye();
		//direction.normalize();
		//osg::Vec3d left = direction ^ osg::Vec3d(0.0, 0.0, 1.0); // Cross product to get left direction
		//left.normalize();
		//setByMatrix(getMatrix() * osg::Matrix::translate(left * 0.1)); // Adjust speed as needed
	}

	void moveRight() {
		//osg::Vec3d direction = getViewMatrixAsLookAt().getLookAt() - getViewMatrixAsLookAt().getEye();
		//direction.normalize();
		//osg::Vec3d right = osg::Vec3d(0.0, 0.0, 1.0) ^ direction; // Cross product to get right direction
		//right.normalize();
		//setByMatrix(getMatrix() * osg::Matrix::translate(right * 0.1)); // Adjust speed as needed
	}
};

class KeyHandler : public osgGA::GUIEventHandler
{
public:
	KeyHandler()
	{}

	bool handle(const osgGA::GUIEventAdapter& ea, osgGA::GUIActionAdapter& aa, osg::Object*, osg::NodeVisitor*)
	{
		switch (ea.getEventType())
		{
			case(osgGA::GUIEventAdapter::KEYDOWN):
			{
				switch (ea.getKey())
				{
				case 'w':
					printf("KEYDOWN W\n");
					break;
				case 'a':
					printf("KEYDOWN A\n");

					break;
				case 's':
					printf("KEYDOWN S\n");

					break;
				case 'd':
					printf("KEYDOWN D\n");

					break;
				default:
					break;
				}

			default:
				break;
			}

			case(osgGA::GUIEventAdapter::KEYUP):
			{
				switch (ea.getKey())
				{
				case 'w':
					printf("KEYUP W\n");
					break;
				case 'a':
					printf("KEYUP A\n");
					break;
				case 's':
					printf("KEYUP S\n");
					break;
				case 'd':
					printf("KEYUP D\n");
					break;
				default:
					break;
				}

				break;
			}
		}

		return false;
	}
};

//#include <osgUtil/SceneView>
//#include <osgUtil/Optimizer>
//#include <osg/Occluder>
//
//void enableOcclusionCulling(osg::Node* node)
//{
//	osg::ref_ptr<osg::StateSet> stateSet = node->getOrCreateStateSet();
//	stateSet->setMode(GL_DEPTH_TEST, osg::StateAttribute::ON);
//
//	// Here we can enable occlusion queries if needed
//	// This is a simplified method, you may want to configure occlusion more thoroughly.
//	osg::ref_ptr<osg::Occluder> occluder = new osg::Occluder;
//	occluder->setOccludedState(GL_QUERY_NO_RESULTS);
//	stateSet->setAttributeAndModes(occluder.get(), osg::StateAttribute::ON);
//}

int main(int argc, char** argv)
{
	TCHAR imgPath[] = L"C:/Games/Grand Theft Auto Vice City/models/gta3.img";
	TCHAR dirPath[] = L"C:/Games/Grand Theft Auto Vice City/models/gta3.dir";

	IMG* imgLoader = new IMG();
	imgLoader->Open(imgPath, dirPath);

	char maps[][MAX_LENGTH_FILENAME] = {
		{ "airport" },
		{ "airportN" },
		{ "bank" },
		{ "bar" },
		{ "bridge" },
		{ "cisland" },
		{ "club" },
		{ "concerth"},
		{ "docks" },
		{ "downtown"},
		{ "downtows"},
		{ "golf" },
		{ "haiti" },
		{ "haitiN" },
		{ "hotel" },
		{ "islandsf" },
		{ "lawyers" },
		{ "littleha" },
		{ "mall" },
		{ "mansion" },
		{ "nbeach" },
		{ "nbeachbt" },
		{ "nbeachw" },
		{ "oceandn" },
		{ "oceandrv" },
		{ "stadint" },
		{ "starisl" },
		{ "stripclb" },
		{ "washintn" },
		{ "washints" },
		{ "yacht" }
	};

	/* Load map models and their textures */
	for (int i = 0; i < sizeof(maps) / sizeof(maps[0]); i++) {
		IDE* ide = new IDE();

		char path[256];
		strcpy(path, "C:/Games/Grand Theft Auto Vice City/data/maps/");
		strcat(path, maps[i]);
		strcat(path, "/");
		strcat(path, maps[i]);
		strcat(path, ".ide");

		int res = ide->Load(path);

		assert(res == 0);

		g_ideFile.push_back(ide);
	}

	IDE* ide = new IDE();
	int res = ide->Load("C:/Games/Grand Theft Auto Vice City/data/maps/generic.ide");
	assert(res == 0);
	g_ideFile.push_back(ide);


	LoadFileTXD("C:/Games/Grand Theft Auto Vice City/models/generic.txd");
	LoadFileTXD("C:/Games/Grand Theft Auto Vice City/models/MISC.txd");
	LoadFileTXD("C:/Games/Grand Theft Auto Vice City/models/particle.txd");

	/* Load from IDE file only archives textures */
	std::vector<string> textures;
	for (int i = 0; i < g_ideFile.size(); i++) {
		int count = g_ideFile[i]->GetCountItems();

		for (int j = 0; j < count; j++) {
			struct itemDefinition* item = &g_ideFile[i]->GetItems()[j];
			textures.push_back(item->textureArchiveName);
		}
	}

	/* Remove dublicate archive textures */
	remove_duplicates(textures);

	/* Load archive textures (TXD files) */
	for (int i = 0; i < textures.size(); i++) {
		LoadFileTXD(imgLoader, textures[i].c_str());
	}

	osg::ref_ptr<osg::Group> root = new osg::Group;


	/* Loading models. IDE file doesn't contain dublicate models */
	for (int i = 0; i < g_ideFile.size(); i++) {
		for (int j = 0; j < g_ideFile[i]->GetCountItems(); j++) {
			struct itemDefinition* itemDef = &g_ideFile[i]->GetItems()[j];

			//osg::Group *group = loadDFF(imgLoader, itemDef->modelName, itemDef->objectId);
			//root->addChild(group);
		}
	}

	for (int i = 0; i < sizeof(maps) / sizeof(maps[0]); i++) {
		char path[256];
		strcpy(path, "C:/Games/Grand Theft Auto Vice City/data/maps/");
		strcat(path, maps[i]);
		strcat(path, "/");
		strcat(path, maps[i]);
		strcat(path, ".ipl");

		IPL* ipl = new IPL();
		ipl->Load(path);
		g_ipl.push_back(ipl);
	}

	for (int i = 0; i < g_ipl.size(); i++) {
		int count = g_ipl[i]->GetCountObjects();

		for (int j = 0; j < count; j++) {
			struct mapItem objectInfo = g_ipl[i]->GetItem(j);

			float x = objectInfo.x;
			float y = objectInfo.y;
			float z = objectInfo.z;

			osg::ref_ptr<osg::Group> rootq = loadDFF(imgLoader, g_ipl[i]->GetItem(j).modelName);

			osg::ref_ptr<osg::MatrixTransform> transform1 = new osg::MatrixTransform;
			osg::Matrix mat;
			mat.identity();
			mat.setTrans(osg::Vec3(x, y, z));
			mat.setRotate(osg::Quat(
				objectInfo.rotation[0],
				objectInfo.rotation[1],
				objectInfo.rotation[2],
				objectInfo.rotation[3] * -1
			));
			mat.scale(osg::Vec3(objectInfo.scale[0], objectInfo.scale[1], objectInfo.scale[2]));
			transform1->setMatrix(mat);
			transform1->addChild(rootq.get());

			root->addChild(transform1.get());

			//if (j >= 0) 
			//	break;
		}
	}

	osg::ref_ptr<osg::MatrixTransform> water = createWater();
	root->addChild(water);

	/*
	osg::ref_ptr<osg::Vec3Array> vertices = new osg::Vec3Array;
	vertices->push_back(osg::Vec3(0.0f, 0.0f, 0.0f));
	vertices->push_back(osg::Vec3(1.0f, 0.0f, 0.0f));
	vertices->push_back(osg::Vec3(1.0f, 0.0f, 1.0f));

	osg::ref_ptr<osg::Vec3Array> normals = new osg::Vec3Array;
	normals->push_back(osg::Vec3(0.0f, -1.0f, 0.0f));

	osg::ref_ptr<osg::Vec4Array> colors = new osg::Vec4Array;
	colors->push_back(osg::Vec4(1.0f, 0.0f, 0.0f, 1.0f));
	colors->push_back(osg::Vec4(0.0f, 1.0f, 0.0f, 1.0f));
	colors->push_back(osg::Vec4(0.0f, 0.0f, 1.0f, 1.0f));

	osg::ref_ptr<osg::Vec2Array> texcoords = new osg::Vec2Array;
	texcoords->push_back(osg::Vec2(0.0f, 0.0f));
	texcoords->push_back(osg::Vec2(0.0f, 1.0f));
	texcoords->push_back(osg::Vec2(1.0f, 1.0f));

	osg::ref_ptr<osg::Geometry> quad = new osg::Geometry;
	quad->setVertexArray(vertices.get());

	quad->setNormalArray(normals.get());
	quad->setNormalBinding(osg::Geometry::BIND_OVERALL);

	quad->setColorArray(colors.get());
	quad->setColorBinding(osg::Geometry::BIND_PER_VERTEX);

	quad->setTexCoordArray(0, texcoords.get());

	quad->addPrimitiveSet(new osg::DrawArrays(GL_TRIANGLES, 0, 3));

	osg::ref_ptr<osg::Texture2D> texture = new osg::Texture2D;
	osg::ref_ptr<osg::Image> image = osgDB::readImageFile("texture-test.bmp");
	// texture->setImage(image.get());
	texture->setImage(gimage);
	*/

	/*{
		osg::ref_ptr<osg::Geode> rootTri = new osg::Geode;
		osg::ref_ptr<osg::MatrixTransform> transform1 = new osg::MatrixTransform;
		transform1->setMatrix(osg::Matrix::translate(0, 0, 0));
		transform1->addChild(rootTri);

		rootTri->addDrawable(quad);
		rootTri->getOrCreateStateSet()->setTextureAttributeAndModes(0, texture.get());


		osg::Group* gr = new osg::Group;

		osg::ref_ptr<osg::LightSource> ls = new osg::LightSource;
		osg::Light* light = new osg::Light;
		light->setAmbient(osg::Vec4(1.0, 1.0, 1.0, 1.0));
		light->setDiffuse(osg::Vec4(1.0, 1.0, 1.0, 1.0));
		light->setSpecular(osg::Vec4(1, 1, 1, 1));  // some examples don't have this one
		ls->setLight(light);

		gr->addChild(ls);

		// root->addChild(gr);
		root->addChild(rootTri);
	}*/


	// if the view matrix is invalid (NaN), use the identity
	// osg::ref_ptr<osg::Camera> camera = viewer.getCamera();
	// if (camera->getViewMatrix().isNaN())
	// camera->setViewMatrix(osg::Matrix::identity());

	 // Create the first-person camera
	//osg::ref_ptr<osg::Camera> firstPersonCamera = new osg::Camera();
	//firstPersonCamera->setProjectionMatrixAsPerspective(45.0f, 3840/2160, 1.0f, 1000.0f);
	//firstPersonCamera->setViewMatrixAsLookAt(osg::Vec3(0.0f, 0.0f, 100.0f), // Camera position (eye)
	//	osg::Vec3(0.0f, 0.0f, 0.0f), // Look at point
	//	osg::Vec3(0.0f, 1.0f, 0.0f)); // Up vector
	//viewer.setCamera(firstPersonCamera);

	// Set the camera manipulator for user input
	//viewer.setCameraManipulator(new osgGA::TrackballManipulator());
	//viewer.setCameraManipulator(new osgGA::FirstPersonManipulator());
	//viewer.setCameraManipulator(new osgGA::FlightManipulator());

	osgViewer::Viewer viewer;
	viewer.setUpViewInWindow(0, 0, 1920, 1080);
	viewer.getCamera()->setClearColor(osg::Vec4(0.49804f, 0.78431f, 0.94510f, 1.0f));
	viewer.getCamera()->setProjectionMatrixAsPerspective(90.0f, 1920 / 1080, 0.1f, 1000.0f);
	
	osg::ref_ptr<CustomFirstPersonManipulator> dm = new CustomFirstPersonManipulator();
	dm->setViewer(&viewer);
	osg::Vec3d eye(0.0, 0.0, 200.0);
	osg::Vec3d center(1000.0, 0.0, 0.0);
	osg::Vec3d up = osg::Z_AXIS;
	dm->setHomePosition(eye, center, up);
	viewer.setCameraManipulator(dm);
	//viewer.getCamera()->setViewMatrixAsLookAt(eye, center, up);

	// viewer.addEventHandler(new KeyHandler());
	
	viewer.getCamera()->setComputeNearFarMode(osg::CullSettings::COMPUTE_NEAR_FAR_USING_BOUNDING_VOLUMES);
	// Turn on FSAA, makes the lines look better.
	
	viewer.setSceneData(root);

	osg::DisplaySettings::instance()->setNumMultiSamples(4);

	printf("[Info] %s loaded\n", PROJECT_NAME);
	
	viewer.realize();

	while (!viewer.done()) {
		viewer.frame();
	}
	
	return 0;
}
