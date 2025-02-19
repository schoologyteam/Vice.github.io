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
#define WINDOW_WIDTH 1024
#define WINDOW_HEIGHT 768
#define WINDOW_TITLE L"openvice"

int frameCount = 0;
Frustum g_frustum;

struct GameMaterial {
	char name[MAX_LENGTH_FILENAME]; /* without extension ".TXD" */
	uint8_t* source;
	int size;
	uint32_t width;
	uint32_t height;
	uint32_t dxtCompression;
	uint32_t depth;
	bool IsAlpha;
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

void LoadAllTexturesFromTXDFile(IMG *pImgLoader, const char *filename)
{
	char result_name[MAX_LENGTH_FILENAME + 4];
	strcpy(result_name, filename);
	strcat(result_name, ".txd");

	int fileId = pImgLoader->GetFileIndexByName(result_name);
	if (fileId == -1) {
		// printf("[Error] Cannot find file %s in IMG archive\n", result_name);
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
				 * X � east/west direction
				 * Y � north/south direction
				 * Z � up/down direction
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

void RenderScene(DXRender *render, Camera *camera)
{
	g_frustum.ConstructFrustum(400.0f, camera->GetProjection(), camera->GetView());

	render->RenderStart();

	int renderCount = 0;

	for (int i = 0; i < g_ipl.size(); i++) {
		int count = g_ipl[i]->GetCountObjects();

		// Render not transparent objects
		for (int j = 0; j < count; j++) {
			struct mapItem objectInfo = g_ipl[i]->GetItem(j);

			float x = objectInfo.x;
			float y = objectInfo.y;
			float z = objectInfo.z;

			bool renderModel = g_frustum.CheckSphere(x, y, z, 50.0f);

			if (renderModel) {
			
				for (int m = 0; m < g_models.size(); m++) {
					Model* model = g_models[m];

					if (model->IsAlpha() == true) {
						continue;
					}

					if (objectInfo.id == model->GetId()) {
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

		// Render transparent objects
		for (int j = 0; j < count; j++) {
			struct mapItem objectInfo = g_ipl[i]->GetItem(j);

			float x = objectInfo.x;
			float y = objectInfo.y;
			float z = objectInfo.z;

			bool renderModel = g_frustum.CheckSphere(x, y, z, 50.0f);

			if (renderModel) {

				for (int m = 0; m < g_models.size(); m++) {
					Model* model = g_models[m];

					if (model->IsAlpha() == false) {
						continue;
					}

					if (objectInfo.id == model->GetId()) {
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
	}

	printf("[Info] Rendered meshes: %d\n", renderCount);

	render->RenderEnd();
}

int WinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPSTR lpCmdLine, _In_ int nCmdShow)
{
	bool vsync = false;

	if (!DirectX::XMVerifyCPUSupport()) {
		MessageBox(NULL, L"You CPU doesn't support DirectXMath", L"Error", MB_OK);
		return 1;
	}

	Window* window = new Window();
	window->Init(hInstance, nCmdShow, WINDOW_WIDTH, WINDOW_HEIGHT, WINDOW_TITLE);

	Input* input = new Input();
	input->Init(hInstance, window->GetHandleWindow());

	Camera* camera = new Camera();
	camera->Init(WINDOW_WIDTH, WINDOW_HEIGHT);

	DXRender* render = new DXRender();
	render->Init(window->GetHandleWindow(), vsync);

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
		{ "docks"},
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


	/* Loading models. IDE file doesn't contain dublicate models */
	for (int i = 0; i < g_ideFile.size(); i++) {
		for (int j = 0; j < g_ideFile[i]->GetCountItems(); j++) {
			struct itemDefinition* itemDef = &g_ideFile[i]->GetItems()[j];
			LoadFileDFFWithName(imgLoader, render, itemDef->modelName, itemDef->objectId);
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

	float moveLeftRight = 0.0f;
	float moveBackForward = 0.0f;

	float camYaw = 0.0f;
	float camPitch = 0.0f;

	DIMOUSESTATE mouseLastState;
	DIMOUSESTATE mouseCurrState;

	mouseCurrState.lX = input->GetMouseSpeedX();
	mouseCurrState.lY = input->GetMouseSpeedY();

	mouseLastState.lX = input->GetMouseSpeedX();
	mouseLastState.lY = input->GetMouseSpeedY();

	
	double frameTime;
	int fps = 0;

	/* main loop */
	MSG msg;
	ZeroMemory(&msg, sizeof(msg));

	while (msg.message != WM_QUIT) {
		if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		} else { /* if have not messages */
			frameCount++;

			if (Utils::GetTime() > 1.0f) {
				fps = frameCount;
				frameCount = 0;
				Utils::StartTimer();
			}

			frameTime = Utils::GetFrameTime();

			input->Detect();

			float speed = 10.0f * frameTime;

			if (input->IsKey(DIK_ESCAPE)) {
				PostQuitMessage(EXIT_SUCCESS);
			}

			if (input->IsKey(DIK_LSHIFT)) {
				speed *= 50;
			}

			if (input->IsKey(DIK_F1)) {
				render->ChangeRasterizerStateToWireframe();
				printf("[Info] Changed render to wireframe\n");
			}

			if (input->IsKey(DIK_F2)) {
				render->ChangeRasterizerStateToSolid();
				printf("[Info] Changed render to solid\n");
			}

			if (input->IsKey(DIK_W)) {
				moveBackForward += speed;
			}

			if (input->IsKey(DIK_A)) {
				moveLeftRight -= speed;
			}

			if (input->IsKey(DIK_S)) {
				moveBackForward -= speed;
			}

			if (input->IsKey(DIK_D)) {
				moveLeftRight += speed;
			}

			mouseCurrState.lX = input->GetMouseSpeedX();
			mouseCurrState.lY = input->GetMouseSpeedY();

			if ((mouseCurrState.lX != mouseLastState.lX)
				|| (mouseCurrState.lY != mouseLastState.lY)) {

				camYaw += mouseLastState.lX * 0.001f;
				camPitch += mouseCurrState.lY * 0.001f;

				mouseLastState = mouseCurrState;
			}

			camera->Update(camPitch, camYaw, moveLeftRight, moveBackForward);

			RenderScene(render, camera);

			moveLeftRight = 0.0f;
			moveBackForward = 0.0f;
		}
	}

	render->Cleanup();
	camera->Cleanup();
	input->Cleanup();

	for (int i = 0; i < g_ipl.size(); i++) {
		g_ideFile[i]->Cleanup();
		delete g_ideFile[i];
	}

	for (int i = 0; i < g_ipl.size(); i++) {
		g_ipl[i]->Cleanup();
		delete g_ipl[i];
	}

	for (int i = 0; i < g_models.size(); i++) {
		g_models[i]->Cleanup();
		delete g_models[i];
	}

	delete camera;
	delete input;
	delete render;

	imgLoader->Cleanup();
	delete imgLoader;

	return msg.wParam;
}
