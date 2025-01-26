#include <string.h>
#include <stdio.h>
#include <algorithm>

#include <windows.h>

#include "renderware.h"
#include "loaders/Clump.h"
#include "loaders/IMG.hpp"
#include "loaders/IPL.hpp"
#include "loaders/IDE.hpp"
#include "Mesh.hpp"
#include "DXRender.hpp"
#include "Camera.hpp"
#include "Input.hpp"
#include "Window.hpp"
#include "Utils.hpp"
#include "Frustum.h"
#include "Model.h"

#define PROJECT_NAME "openvice"
#define WINDOW_WIDTH 1920
#define WINDOW_HEIGHT 1080
#define WINDOW_TITLE L"openvice"

int frameCount = 0;
Frustum g_frustum;

#include <osg/Texture2D>
#include <osg/MatrixTransform>
#include <osgDB/ReadFile>

#include <osg/ShapeDrawable>
#include <osgViewer/Viewer>
#include <osg/Geometry>
#include <osg/Geode>
#include <osg/PolygonMode>

#include "ReaderWriterDDS.cpp"

osg::Texture2D* gtexture = NULL;
osg::Image* gimage = NULL;

struct GameMaterial {
	char name[MAX_LENGTH_FILENAME]; /* without extension ".TXD" */
	uint8_t* source;
	int size;
	uint32_t width;
	uint32_t height;
	uint32_t dxtCompression;
	uint32_t depth;
	bool IsAlpha;
	osg::Image* image;
};

struct ModelMaterial {
	char materialName[MAX_LENGTH_FILENAME];
	int index;
};

std::vector<Model*> g_models;
std::vector<IDE*> g_ideFile;
std::vector<GameMaterial> g_Textures;
std::vector<IPL*> g_ipl;

template <typename T>
void remove_duplicates(std::vector<T>& vec)
{
	std::sort(vec.begin(), vec.end());
	vec.erase(std::unique(vec.begin(), vec.end()), vec.end());
}


// uint8_t*
char* manualCreateDds(uint8_t* pDataSourceDDS, size_t fileSizeDDS, uint32_t width, uint32_t height, uint32_t dxtCompression, uint32_t depth)
{
	/* Manual create DDS file */
	struct DDS_File dds;
	dds.dwMagic = DDS_MAGIC;
	dds.header.size = sizeof(struct DDS_HEADER);
	dds.header.flags = 0; // 0
	dds.header.width = width;
	dds.header.height = height;
	dds.header.pitchOrLinearSize = width * height;
	dds.header.mipMapCount = 0;
	dds.header.ddspf.size = sizeof(struct DDS_PIXELFORMAT);
	//ddsd.ddpfPixelFormat.dwSize = sizeof(ddsd.ddpfPixelFormat);
	dds.header.ddspf.flags = DDS_FOURCC; // DDS_PAL8; // TODO: use DDS_HEADER_FLAGS_VOLUME for depth
	//dds.header.depth = depth; // TODO: is working?
	switch (dxtCompression) {
	default:
	case 1:
		dds.header.ddspf.fourCC = FOURCC_DXT1;
		break;
	case 3:
		dds.header.ddspf.fourCC = FOURCC_DXT3;
		break;
	case 4:
		dds.header.ddspf.fourCC = FOURCC_DXT4;
		break;
	}
	// ddsd.ddpfPixelFormat.dwFourCC = bpp == 24 ? FOURCC_DXT1 : FOURCC_DXT5;

	size_t len = sizeof(struct DDS_File) + fileSizeDDS;
	// было в uint8_t*
	char* buf = (char*)malloc(len);
	memcpy(buf, &dds, sizeof(dds));
	memcpy(
		buf + sizeof(dds), // dds + offset
		pDataSourceDDS,
		fileSizeDDS
	);

	return buf;
}

struct membuf : std::streambuf
{
	membuf(char* begin, char* end) {
		this->setg(begin, begin, end);
	}
};

void LoadAllTexturesFromTXDFile(IMG *pImgLoader, const char *filename)
{
	char result_name[MAX_LENGTH_FILENAME + 4];
	strcpy(result_name, filename);
	strcat(result_name, ".txd");

	int fileId = pImgLoader->GetFileIndexByName(result_name);
	if (fileId == -1) {
		printf("[Error] Cannot find file %s in IMG archive\n", result_name);
		return;
	}

	// printf("[Info] Loading file %s from IMG archive\n", filename);

	char *fileBuffer = (char*)pImgLoader->GetFileById(fileId);

	size_t offset = 0;
	TextureDictionary txd;
	txd.read(fileBuffer, &offset);

	/* Loop for every texture in TXD file */
	for (uint32_t i = 0; i < txd.texList.size(); i++) {
		NativeTexture &t = txd.texList[i];
		// printf("%s %s %d %d %d %d\n", t.name, t.maskName.c_str(), t.width[0], t.height[0], t.depth, t.rasterFormat);
		
		uint8_t* texelsToArray = t.texels[0];
		size_t len = t.dataSizes[0];

		struct GameMaterial m;
		memcpy(m.name, t.name, sizeof(t.name)); /* without extension ".TXD" */

		/* TODO: Replace copy to buffer to best solution */
		/* TODO: Free memory */
		m.source = (uint8_t *)malloc(len);
		memcpy(m.source, texelsToArray, len);

		m.size = t.dataSizes[0];
		m.width = t.width[0];
		m.height = t.height[0];
		m.dxtCompression = t.dxtCompression; /* DXT1, DXT3, DXT4 */
		m.depth = t.depth;
		m.IsAlpha = t.IsAlpha;

		// printf("[OK] Loaded texture name %s from TXD file %s\n", t.name, result_name);

		
		// manual create DDS file from buffer
		//char *fileBuf = manualCreateDds(m.source, len, m.width, m.height, m.dxtCompression, m.depth);

		// convert buffer to istream
		/*int size_t = sizeof(struct DDS_File) + m.size;
		std::vector<uint8_t> data(fileBuf[0], fileBuf[len]);
		imemstream stream(reinterpret_cast<const char*>(data.data()), data.size());*/
		
		
		
		//membuf sbuf(fileBuf, fileBuf + sizeof(fileBuf));
		//std::istream in(&sbuf);

		// load dds file from istream
		//osg::Image *image = ReadDDSFile(in, false);
		//if (image == NULL) {
		//	printf("image = NULL \n");
		//}

		//m.image = image;

		g_Textures.push_back(m);
	}

	//free(fileBuffer);
}


int LoadFileDFFWithName(IMG* pImgLoader, DXRender* render, char *name, int modelId)
{
	/* Skip LOD files */
	if (strstr(name, "LOD") != NULL) {
		return 0;
	}

	char result_name[MAX_LENGTH_FILENAME + 4];
	strcpy(result_name, name);
	strcat(result_name, ".dff");

	int fileId = pImgLoader->GetFileIndexByName(result_name);
	if (fileId == -1) {
		return 1;
	}

	char* fileBuffer = (char*)pImgLoader->GetFileById(fileId);

	Clump* clump = new Clump();
	clump->Read(fileBuffer);

	Model* model = new Model();
	model->SetId(modelId);
	model->SetName(name);

	for (uint32_t index = 0; index < clump->m_numGeometries; index++) {

		std::vector<ModelMaterial> materIndex;

		/* Load all materials */
		uint32_t materials = clump->GetGeometryList()[index]->m_numMaterials;
		for (uint32_t i = 0; i < materials; i++) {
			Material *material = clump->GetGeometryList()[index]->materialList[i];

			struct ModelMaterial matInd;
			std::string b = material->texture.name;
			//matInd.materialName = b;
			memcpy(matInd.materialName, b.c_str(), sizeof(matInd.materialName));
			matInd.index = i;

			materIndex.push_back(matInd);
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

			int v_count = clump->GetGeometryList()[index]->vertexCount;

			/* Save to data for create vertex buffer (x,y,z tx,ty) */
			// TODO: Free memory
			float *meshVertexData = (float*)malloc(sizeof(float) * v_count * 5);

			for (int v = 0; v < v_count; v++) {
				float x = clump->GetGeometryList()[index]->vertices[v * 3 + 0];
				float y = clump->GetGeometryList()[index]->vertices[v * 3 + 1];
				float z = clump->GetGeometryList()[index]->vertices[v * 3 + 2];

				float tx = 0.0f;
				float ty = 0.0f;
				if (clump->GetGeometryList()[index]->flags & FLAGS_TEXTURED) {
					tx = clump->GetGeometryList()[index]->texCoords[0][v * 2 + 0];
					ty = clump->GetGeometryList()[index]->texCoords[0][v * 2 + 1];
				}

				/*
				 * Flip coordinates. We use Left Handed Coordinates,
				 * but GTA engine use own coordinate system:
				 * X Ц east/west direction
				 * Y Ц north/south direction
				 * Z Ц up/down direction
				 * @see https://gtamods.com/wiki/Map_system
				*/
				meshVertexData[v * 5 + 0] = x;
				meshVertexData[v * 5 + 1] = z;
				meshVertexData[v * 5 + 2] = y;

				meshVertexData[v * 5 + 3] = tx;
				meshVertexData[v * 5 + 4] = ty;
			}

			D3D_PRIMITIVE_TOPOLOGY topology =
				clump->GetGeometryList()[index]->faceType == FACETYPE_STRIP
				? D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP
				: D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

			Mesh* mesh = new Mesh();

			mesh->Init(
				render, 
				meshVertexData,
				v_count * 5,
				(unsigned int*)clump->GetGeometryList()[index]->splits[i].indices,
				clump->GetGeometryList()[index]->splits[i].m_numIndices,
				topology
			);

			uint32_t materialIndex = clump->GetGeometryList()[index]->splits[i].matIndex;

			int matIndex = -1;

			// Find texture by index
			for (int ib = 0; ib < materIndex.size(); ib++) {

				if (materialIndex == materIndex[ib].index) {

					for (int im = 0; im < g_Textures.size(); im++) {
						if (strcmp(g_Textures[im].name, materIndex[ib].materialName) == 0) {
							matIndex = im;
							break;
						}
					}

				}
			}

			if (matIndex != -1) {
				mesh->SetAlpha(g_Textures[matIndex].IsAlpha);
				
				if (g_Textures[matIndex].IsAlpha) {
					model->SetAlpha(true);
				}

				mesh->SetDataDDS(
					render,
					g_Textures[matIndex].source,
					g_Textures[matIndex].size,
					g_Textures[matIndex].width,
					g_Textures[matIndex].height,
					g_Textures[matIndex].dxtCompression,
					g_Textures[matIndex].depth
				);
			}

			model->AddMesh(mesh);
		}
	}

	clump->Clear();
	delete clump;

	g_models.push_back(model);

	return 0;
}




inline float Distance(XMVECTOR v1, XMVECTOR v2)
{
	return XMVectorGetX(XMVector3Length(XMVectorSubtract(v1, v2)));
}
inline float DistanceSquared(XMVECTOR v1, XMVECTOR v2)
{
	return XMVectorGetX(XMVector3LengthSq(XMVectorSubtract(v1, v2)));
}

struct rend {
	int id;
	int dist;

	struct mapItem obinfo;
};

std::vector<struct rend> gNeedRender;

// Comparator function
bool comp(const struct rend left, const struct rend right) {
	return left.dist > right.dist;
}

#define MAX_RENDER_DISTANCE 100

void RenderScene(DXRender *render, Camera *camera)
{
	g_frustum.ConstructFrustum(400.0f, camera->GetProjection(), camera->GetView());

	render->RenderStart();

	int renderCount = 0;

	for (int i = 0; i < g_ipl.size(); i++) {
		int count = g_ipl[i]->GetCountObjects();

		for (int j = 0; j < count; j++) {
			struct mapItem objectInfo = g_ipl[i]->GetItem(j);

			float x = objectInfo.x;
			float y = objectInfo.y;
			float z = objectInfo.z;

			XMVECTOR cameraPos = camera->GetPosition();

			float distance = DistanceSquared(cameraPos, XMVectorSet(x, y, z, 0));

			if ((int)distance < MAX_RENDER_DISTANCE * 1000) {
				struct rend rr;
				rr.id = objectInfo.id;
				rr.dist = (int)distance;
				rr.obinfo = objectInfo;
				gNeedRender.push_back(rr);
			}
		}
	}

	// Sort vector in order
	std::sort(gNeedRender.begin(), gNeedRender.end(), comp);

	for (int m = 0; m < g_models.size(); m++) {
		Model* model = g_models[m];

		for (int i = 0; i < gNeedRender.size(); i++) {
			struct mapItem objectInfo = gNeedRender[i].obinfo;

			if (gNeedRender[i].id == model->GetId()) {
				bool renderModel = g_frustum.CheckSphere(objectInfo.x, objectInfo.y, objectInfo.z, 50.0f);

				if (renderModel) {
					model->SetPosition(
						objectInfo.x, objectInfo.y, objectInfo.z,
						objectInfo.scale[0], objectInfo.scale[1], objectInfo.scale[2],
						objectInfo.rotation[0], objectInfo.rotation[1], objectInfo.rotation[2], objectInfo.rotation[3]
					);
					model->Render(render, camera);

					renderCount++;
				}
			}
		}
	}

	printf("[Info] Rendered meshes: %d\n", renderCount);

	render->RenderEnd();

	gNeedRender.clear();
}

#include <iostream>
#include <GL/gl.h>

// Function to check for OpenGL errors
void checkOpenGLError(const std::string& functionName) {
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

osg::Group* loadDFF(IMG* imgLoader, char *name, int modelId = 0)
{
	// checkOpenGLError("loadDFF");

	/* Skip LOD files */
	if (strstr(name, "LOD") != NULL) {
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


	osg::Group *root = new osg::Group;


	Clump* clump = new Clump();
	clump->Read(fileBuffer);

	for (uint32_t index = 0; index < clump->m_numGeometries; index++) {
		
		osg::ref_ptr<osg::Geode> geode = new osg::Geode;

		std::vector<ModelMaterial> materIndex;

		/* Load all materials */
		uint32_t materials = clump->GetGeometryList()[index]->m_numMaterials;
		for (uint32_t i = 0; i < materials; i++) {
			Material* material = clump->GetGeometryList()[index]->materialList[i];

			struct ModelMaterial matInd;
			std::string b = material->texture.name;
			//matInd.materialName = b;
			memcpy(matInd.materialName, b.c_str(), sizeof(matInd.materialName));
			matInd.index = i;

			materIndex.push_back(matInd);
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
			// osg::ref_ptr<osg::Vec3Array> normals = new osg::Vec3Array;

			int v_count = clump->GetGeometryList()[index]->vertexCount;

			for (int v = 0; v < v_count; v++) {
				float x = clump->GetGeometryList()[index]->vertices[v * 3 + 0];
				float y = clump->GetGeometryList()[index]->vertices[v * 3 + 1];
				float z = clump->GetGeometryList()[index]->vertices[v * 3 + 2];

				if (clump->GetGeometryList()[index]->flags & FLAGS_NORMALS) {
					float nx = clump->GetGeometryList()[index]->normals[v * 3 + 0];
					float ny = clump->GetGeometryList()[index]->normals[v * 3 + 1];
					float nz = clump->GetGeometryList()[index]->normals[v * 3 + 2];

					// normals->push_back(osg::Vec3(nx, ny, nz));
				}
				

				float tx = 0.0f;
				float ty = 0.0f;
				if (clump->GetGeometryList()[index]->flags & FLAGS_TEXTURED) {
					tx = clump->GetGeometryList()[index]->texCoords[0][v * 2 + 0];
					ty = clump->GetGeometryList()[index]->texCoords[0][v * 2 + 1];

					texcoords->push_back(osg::Vec2(tx, ty));
				}

				/*
				 * Flip coordinates. We use Left Handed Coordinates,
				 * but GTA engine use own coordinate system:
				 * X Ц east/west direction
				 * Y Ц north/south direction
				 * Z Ц up/down direction
				 * @see https://gtamods.com/wiki/Map_system
				*/
				vertices->push_back(osg::Vec3(x, y, z));
			}

			osg::ref_ptr<osg::DrawElementsUInt> indices = new osg::DrawElementsUInt(
				clump->GetGeometryList()[index]->faceType == FACETYPE_STRIP
				? osg::PrimitiveSet::TRIANGLE_STRIP // D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP
				: osg::PrimitiveSet::TRIANGLES // D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST
				,
				clump->GetGeometryList()[index]->splits[i].m_numIndices
			);

			//indices->addElement((unsigned int)clump->GetGeometryList()[index]->splits[i].indices);

			memcpy((void*)indices->getDataPointer(), (unsigned int*)clump->GetGeometryList()[index]->splits[i].indices,
				sizeof(unsigned int) * clump->GetGeometryList()[index]->splits[i].m_numIndices);

			geometry->setVertexArray(vertices);
			geometry->setTexCoordArray(0, texcoords);
			//if (clump->GetGeometryList()[index]->flags & FLAGS_NORMALS) {
			//	quad->setNormalBinding(osg::Geometry::BIND_OVERALL);
			//}
			geometry->addPrimitiveSet(indices);

			//osg::Texture2D *texture = new osg::Texture2D;
			//osg::Image *image = osgDB::readImageFile("texture-test2.bmp");
			//texture->setImage(image);

			
			//osg::ref_ptr<osg::Texture2D> texture = new osg::Texture2D;
			//texture->setImage(gimage);


			// wireframe
			//osg::ref_ptr<osg::PolygonMode> pm = new osg::PolygonMode;
			//pm->setMode(osg::PolygonMode::FRONT_AND_BACK, osg::PolygonMode::LINE);
			//quad->getOrCreateStateSet()->setAttribute(pm.get());




			
			// osg::ref_ptr<osg::Image> image = osgDB::readImageFile("../data/Images/lz.rgb");
			//osg::ref_ptr<osg::Image> image = ReadDDSFile(null, false);
			//texture->setImage(gimage);




			//osg::ref_ptr<osg::Texture2D> texture = new osg::Texture2D;

			uint32_t materialIndex = clump->GetGeometryList()[index]->splits[i].matIndex;

			int matIndex = -1;

			// Find texture by index
			for (int ib = 0; ib < materIndex.size(); ib++) {

				if (materialIndex == materIndex[ib].index) {

					for (int im = 0; im < g_Textures.size(); im++) {
						if (strcmp(g_Textures[im].name, materIndex[ib].materialName) == 0) {
							matIndex = im;
							break;
						}
					}

				}
			}

			if (matIndex != -1) {
				//mesh->SetAlpha(g_Textures[matIndex].IsAlpha);

				//if (g_Textures[matIndex].IsAlpha) {
					//model->SetAlpha(true);
				//}

				// osg::ref_ptr<osg::Image> image = osgDB::readImageFile("../data/Images/lz.rgb");
				//osg::ref_ptr<osg::Image> image = ReadDDSFile(null, false);
				//texture->setImage(g_Textures[matIndex].image);
				/*mesh->SetDataDDS(
						render,
						g_Textures[matIndex].source,
						g_Textures[matIndex].size,
						g_Textures[matIndex].width,
						g_Textures[matIndex].height,
						g_Textures[matIndex].dxtCompression,
						g_Textures[matIndex].depth
				);*/
			}

			// gtexture->setFilter(osg::Texture::MIN_FILTER, osg::Texture::LINEAR);
			// gtexture->setFilter(osg::Texture::MAG_FILTER, osg::Texture::LINEAR);

			// Create a StateSet to apply the texture

			if (clump->GetGeometryList()[index]->flags & FLAGS_TEXTURED) {
				osg::ref_ptr<osg::StateSet> stateSet = geode->getOrCreateStateSet();
				stateSet->setTextureAttributeAndModes(0, gtexture, osg::StateAttribute::ON);
				// stateSet->setMode(GL_LIGHTING, osg::StateAttribute::OFF); // Disable lighting for this example
			}
			geode->addDrawable(geometry);		
			//root2->getOrCreateStateSet()->setTextureAttributeAndModes(0, gtexture);
		}

		root->addChild(geode);
	}

	

	//clump->Clear();
	//delete clump;

	return root;
}


int main(int argc, char **argv)
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
		LoadAllTexturesFromTXDFile(imgLoader, textures[i].c_str());
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

	printf("[Info] %s loaded\n", PROJECT_NAME);


	gtexture = new osg::Texture2D;
	gimage = osgDB::readImageFile("texture-test2.bmp");
	gtexture->setImage(gimage);

	for (int i = 0; i < g_ipl.size(); i++) {
		int count = g_ipl[i]->GetCountObjects();

		for (int j = 0; j < count; j++) {
			struct mapItem objectInfo = g_ipl[i]->GetItem(j);

			float x = objectInfo.x;
			float y = objectInfo.y;
			float z = objectInfo.z;

			osg::ref_ptr<osg::Group> rootq = loadDFF(imgLoader, g_ipl[i]->GetItem(j).modelName, g_ipl[i]->GetItem(j).id);
			
			osg::ref_ptr<osg::MatrixTransform> transform1 = new osg::MatrixTransform;
			transform1->setMatrix(osg::Matrix::translate(x, z, y)); // X Z Y
			transform1->addChild(rootq);

			root->addChild(transform1);
		}
	}

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

	

	osg::ref_ptr<osg::Geode> rootTri = new osg::Geode;
	osg::ref_ptr<osg::MatrixTransform> transform1 = new osg::MatrixTransform;
	transform1->setMatrix(osg::Matrix::translate(0, 0, 0));
	transform1->addChild(rootTri);
	
	rootTri->addDrawable(quad);
	rootTri->getOrCreateStateSet()->setTextureAttributeAndModes(0, texture.get());

	root->addChild(rootTri);


	osgViewer::Viewer viewer;
	viewer.setUpViewInWindow(0, 0, 1920, 1080);

	// if the view matrix is invalid (NaN), use the identity
	// osg::ref_ptr<osg::Camera> camera = viewer.getCamera();
	// if (camera->getViewMatrix().isNaN())
	// camera->setViewMatrix(osg::Matrix::identity());

	viewer.setSceneData(root);
	
	return viewer.run();

	/* viewer.realize();
	while (!viewer.done()) {
		viewer.frame();
	}
	
	return 0; */
}
