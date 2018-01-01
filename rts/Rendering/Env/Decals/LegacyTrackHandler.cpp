/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#include "LegacyTrackHandler.h"

#include "Game/Camera.h"
#include "Game/GlobalUnsynced.h"
#include "Map/HeightMapTexture.h"
#include "Map/Ground.h"
#include "Map/MapInfo.h"
#include "Map/ReadMap.h"
#include "Rendering/GlobalRendering.h"
#include "Rendering/ShadowHandler.h"
#include "Rendering/Env/ISky.h"
#include "Rendering/Env/SunLighting.h"
#include "Rendering/GL/myGL.h"
#include "Rendering/GL/RenderDataBuffer.hpp"
#include "Rendering/Map/InfoTexture/IInfoTextureHandler.h"
#include "Rendering/Shaders/ShaderHandler.h"
#include "Rendering/Shaders/Shader.h"
#include "Rendering/Textures/Bitmap.h"
#include "Sim/Units/UnitDef.h"
#include "System/ContainerUtil.h"
#include "System/EventHandler.h"
#include "System/TimeProfiler.h"
#include "System/StringUtil.h"
#include "System/FileSystem/FileSystem.h"
#include "System/Log/ILog.h"

#include <algorithm>
#include <cctype>



LegacyTrackHandler::LegacyTrackHandler()
	: CEventClient("[LegacyTrackHandler]", 314160, false)
{
	eventHandler.AddClient(this);
	LoadDecalShaders();
}



LegacyTrackHandler::~LegacyTrackHandler()
{
	eventHandler.RemoveClient(this);

	for (TrackType& tt: trackTypes) {
		for (UnitTrackStruct* uts: tt.tracks)
			spring::VectorInsertUnique(tracksToBeDeleted, uts, true);

		glDeleteTextures(1, &tt.texture);
	}

	for (auto ti = tracksToBeAdded.cbegin(); ti != tracksToBeAdded.cend(); ++ti)
		spring::VectorInsertUnique(tracksToBeDeleted, *ti, true);

	for (auto ti = tracksToBeDeleted.cbegin(); ti != tracksToBeDeleted.cend(); ++ti)
		delete *ti;

	trackTypes.clear();
	tracksToBeAdded.clear();
	tracksToBeDeleted.clear();

	#ifndef USE_DECALHANDLER_STATE
	shaderHandler->ReleaseProgramObjects("[LegacyTrackHandler]");
	#endif
}


void LegacyTrackHandler::LoadDecalShaders()
{
	#ifndef USE_DECALHANDLER_STATE
	#define sh shaderHandler
	decalShaders.fill(nullptr);

	const std::string extraDef = "#define HAVE_SHADING_TEX " + IntToString(readMap->GetShadingTexture() != 0, "%d") + "\n";
	const float4 invMapSize = {
		1.0f / (mapDims.pwr2mapx * SQUARE_SIZE),
		1.0f / (mapDims.pwr2mapy * SQUARE_SIZE),
		1.0f / (mapDims.mapx * SQUARE_SIZE),
		1.0f / (mapDims.mapy * SQUARE_SIZE),
	};

	decalShaders[DECAL_SHADER_NULL] = Shader::nullProgramObject;
	decalShaders[DECAL_SHADER_CURR] = decalShaders[DECAL_SHADER_NULL];
	decalShaders[DECAL_SHADER_GLSL] = sh->CreateProgramObject("[LegacyTrackHandler]", "DecalShaderGLSL");

	decalShaders[DECAL_SHADER_GLSL]->AttachShaderObject(sh->CreateShaderObject("GLSL/GroundDecalsVertProg.glsl", "",       GL_VERTEX_SHADER));
	decalShaders[DECAL_SHADER_GLSL]->AttachShaderObject(sh->CreateShaderObject("GLSL/GroundDecalsFragProg.glsl", extraDef, GL_FRAGMENT_SHADER));
	decalShaders[DECAL_SHADER_GLSL]->Link();

	decalShaders[DECAL_SHADER_GLSL]->SetFlag("HAVE_SHADOWS", false);

	decalShaders[DECAL_SHADER_GLSL]->SetUniformLocation("decalTex");           // idx  0
	decalShaders[DECAL_SHADER_GLSL]->SetUniformLocation("shadeTex");           // idx  1
	decalShaders[DECAL_SHADER_GLSL]->SetUniformLocation("shadowTex");          // idx  2
	decalShaders[DECAL_SHADER_GLSL]->SetUniformLocation("heightTex");          // idx  3
	decalShaders[DECAL_SHADER_GLSL]->SetUniformLocation("mapSizePO2");         // idx  4
	decalShaders[DECAL_SHADER_GLSL]->SetUniformLocation("groundAmbientColor"); // idx  5
	decalShaders[DECAL_SHADER_GLSL]->SetUniformLocation("viewMatrix");         // idx  6
	decalShaders[DECAL_SHADER_GLSL]->SetUniformLocation("projMatrix");         // idx  7
	decalShaders[DECAL_SHADER_GLSL]->SetUniformLocation("quadMatrix");         // idx  8
	decalShaders[DECAL_SHADER_GLSL]->SetUniformLocation("shadowMatrix");       // idx  9
	decalShaders[DECAL_SHADER_GLSL]->SetUniformLocation("shadowParams");       // idx 10
	decalShaders[DECAL_SHADER_GLSL]->SetUniformLocation("shadowDensity");      // idx 11
	decalShaders[DECAL_SHADER_GLSL]->SetUniformLocation("decalAlpha");         // idx 12

	decalShaders[DECAL_SHADER_GLSL]->Enable();
	decalShaders[DECAL_SHADER_GLSL]->SetUniform1i(0, 0); // decalTex  (idx 0, texunit 0)
	decalShaders[DECAL_SHADER_GLSL]->SetUniform1i(1, 1); // shadeTex  (idx 1, texunit 1)
	decalShaders[DECAL_SHADER_GLSL]->SetUniform1i(2, 2); // shadowTex (idx 2, texunit 2)
	decalShaders[DECAL_SHADER_GLSL]->SetUniform1i(3, 3); // heightTex (idx 3, texunit 3)
	decalShaders[DECAL_SHADER_GLSL]->SetUniform4f(4, invMapSize.x, invMapSize.y, invMapSize.z, invMapSize.w);
	decalShaders[DECAL_SHADER_GLSL]->SetUniform1f(11, sunLighting->groundShadowDensity);
	decalShaders[DECAL_SHADER_GLSL]->SetUniform1f(12, 1.0f);
	decalShaders[DECAL_SHADER_GLSL]->Disable();
	decalShaders[DECAL_SHADER_GLSL]->Validate();

	decalShaders[DECAL_SHADER_CURR] = decalShaders[DECAL_SHADER_GLSL];
	#undef sh
	#endif
}

void LegacyTrackHandler::SunChanged()
{
	#ifndef USE_DECALHANDLER_STATE
	decalShaders[DECAL_SHADER_GLSL]->Enable();
	decalShaders[DECAL_SHADER_GLSL]->SetUniform1f(11, sunLighting->groundShadowDensity);
	decalShaders[DECAL_SHADER_GLSL]->Disable();
	#endif
}


void LegacyTrackHandler::AddTracks()
{
	// Delayed addition of new tracks
	for (const UnitTrackStruct* uts: tracksToBeAdded) {
		const CUnit* unit = uts->owner;
		const TrackPart& tp = uts->lastAdded;

		// check if RenderUnitDestroyed pre-empted us
		if (unit == nullptr)
			continue;

		// if the unit is moving in a straight line only place marks at half the rate by replacing old ones
		bool replace = false;

		if (unit->myTrack->parts.size() > 1) {
			std::deque<TrackPart>::iterator last = --unit->myTrack->parts.end();
			std::deque<TrackPart>::iterator prev = last--;

			replace = (((tp.pos1 + (*last).pos1) * 0.5f).SqDistance((*prev).pos1) < 1.0f);
		}

		if (replace) {
			unit->myTrack->parts.back() = tp;
		} else {
			unit->myTrack->parts.push_back(tp);
		}
	}

	tracksToBeAdded.clear();

	for (UnitTrackStruct* uts: tracksToBeDeleted)
		delete uts;

	tracksToBeDeleted.clear();
}


void LegacyTrackHandler::DrawTracks(GL::RenderDataBufferTC* buffer, Shader::IProgramObject* shader)
{
	unsigned char curPartColor[4] = {255, 255, 255, 255};
	unsigned char nxtPartColor[4] = {255, 255, 255, 255};

	shader->SetUniform1f(12, 1.0f);
	shader->SetUniformMatrix4fv(8, false, CMatrix44f::Identity());

	// create and draw the unit footprint quads
	for (TrackType& tt: trackTypes) {
		if (tt.tracks.empty())
			continue;


		glBindTexture(GL_TEXTURE_2D, tt.texture);

		for (UnitTrackStruct* track: tt.tracks) {
			if (track->parts.empty()) {
				tracksToBeCleaned.push_back(TrackToClean(track, &(tt.tracks)));
				continue;
			}

			if (gs->frameNum > ((track->parts.front()).creationTime + track->lifeTime)) {
				tracksToBeCleaned.push_back(TrackToClean(track, &(tt.tracks)));
				// still draw the track to avoid flicker
				// continue;
			}

			const auto frontPart = track->parts.front();
			const auto  backPart = track->parts.back();

			if (!camera->InView((frontPart.pos1 + backPart.pos1) * 0.5f, frontPart.pos1.distance(backPart.pos1) + 500.0f))
				continue;

			// walk across the track parts from front (oldest) to back (newest) and draw
			// a quad between "connected" parts (ie. parts differing 8 sim-frames in age)
			std::deque<TrackPart>::const_iterator curPartIt =   (track->parts.begin());
			std::deque<TrackPart>::const_iterator nxtPartIt = ++(track->parts.begin());

			curPartColor[3] = std::max(0.0f, 255.0f - (gs->frameNum - (*curPartIt).creationTime) * track->alphaFalloff);

			for (; nxtPartIt != track->parts.end(); ++nxtPartIt) {
				const TrackPart& curPart = *curPartIt;
				const TrackPart& nxtPart = *nxtPartIt;

				nxtPartColor[3] = std::max(0.0f, 255.0f - (gs->frameNum - nxtPart.creationTime) * track->alphaFalloff);

				if (nxtPart.connected) {
					buffer->SafeAppend({curPart.pos1, curPart.texPos, 0.0f, curPartColor});
					buffer->SafeAppend({curPart.pos2, curPart.texPos, 1.0f, curPartColor});
					buffer->SafeAppend({nxtPart.pos2, nxtPart.texPos, 1.0f, nxtPartColor});
					buffer->SafeAppend({nxtPart.pos1, nxtPart.texPos, 0.0f, nxtPartColor});
				}

				curPartColor[3] = nxtPartColor[3];
				curPartIt = nxtPartIt;
			}
		}

		buffer->Submit(GL_QUADS);
	}
}

void LegacyTrackHandler::CleanTracks()
{
	// Cleanup old tracks; runs *immediately* after DrawTracks
	for (TrackToClean& ttc: tracksToBeCleaned) {
		UnitTrackStruct* track = ttc.track;

		while (!track->parts.empty()) {
			// stop at the first part that is still too young for deletion
			if (gs->frameNum < ((track->parts.front()).creationTime + track->lifeTime))
				break;

			track->parts.pop_front();
		}

		if (track->parts.empty()) {
			if (track->owner != nullptr) {
				track->owner->myTrack = nullptr;
				track->owner = nullptr;
			}

			spring::VectorErase(*ttc.tracks, track);
			tracksToBeDeleted.push_back(track);
		}
	}

	tracksToBeCleaned.clear();
}


bool LegacyTrackHandler::GetDrawTracks() const
{
	//FIXME move track updating to ::Update()
	if ((!tracksToBeAdded.empty()) || (!tracksToBeCleaned.empty()) || (!tracksToBeDeleted.empty()))
		return true;

	for (auto& tt: trackTypes) {
		if (!tt.tracks.empty())
			return true;
	}

	return false;
}


void LegacyTrackHandler::Draw(Shader::IProgramObject* shader)
{
	SCOPED_TIMER("Draw::World::Decals::Tracks");

	if (!GetDrawTracks())
		return;

	#ifndef USE_DECALHANDLER_STATE
	{
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		glEnable(GL_POLYGON_OFFSET_FILL);
		glDepthMask(0);
		glPolygonOffset(-10.0f, -20.0f);

		BindTextures();
		BindShader(sunLighting->groundAmbientColor * CGlobalRendering::SMF_INTENSITY_MULT);

		AddTracks();
		DrawTracks(GL::GetRenderBufferTC(), decalShaders[DECAL_SHADER_CURR]);
		CleanTracks();

		decalShaders[DECAL_SHADER_CURR]->Disable();
		KillTextures();

		glDisable(GL_POLYGON_OFFSET_FILL);
		glDisable(GL_BLEND);
	}
	#else
	{
		AddTracks();
		DrawTracks(GL::GetRenderBufferTC(), shader);
		CleanTracks();
	}
	#endif
}


void LegacyTrackHandler::BindTextures()
{
	{
		glActiveTexture(GL_TEXTURE3);
		glBindTexture(GL_TEXTURE_2D, heightMapTexture->GetTextureID());

		glActiveTexture(GL_TEXTURE1);
		glBindTexture(GL_TEXTURE_2D, readMap->GetShadingTexture());
	}

	if (shadowHandler->ShadowsLoaded())
		shadowHandler->SetupShadowTexSampler(GL_TEXTURE2, true);

	glActiveTexture(GL_TEXTURE0);
}


void LegacyTrackHandler::KillTextures()
{
	glActiveTexture(GL_TEXTURE3);
	glBindTexture(GL_TEXTURE_2D, 0);

	if (shadowHandler->ShadowsLoaded())
		shadowHandler->ResetShadowTexSampler(GL_TEXTURE2, true);

	glActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_2D, 0);

	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, 0);
}


void LegacyTrackHandler::BindShader(const float3& ambientColor)
{
	#ifndef USE_DECALHANDLER_STATE
	decalShaders[DECAL_SHADER_CURR]->SetFlag("HAVE_SHADOWS", shadowHandler->ShadowsLoaded());
	decalShaders[DECAL_SHADER_CURR]->Enable();

	if (decalShaders[DECAL_SHADER_CURR] == decalShaders[DECAL_SHADER_GLSL]) {
		decalShaders[DECAL_SHADER_CURR]->SetUniform4f(5, ambientColor.x, ambientColor.y, ambientColor.z, 1.0f);
		decalShaders[DECAL_SHADER_CURR]->SetUniformMatrix4fv(6, false, camera->GetViewMatrix());
		decalShaders[DECAL_SHADER_CURR]->SetUniformMatrix4fv(7, false, camera->GetProjectionMatrix());
		decalShaders[DECAL_SHADER_CURR]->SetUniformMatrix4fv(9, false, shadowHandler->GetShadowViewMatrixRaw());
		decalShaders[DECAL_SHADER_CURR]->SetUniform4fv(10, shadowHandler->GetShadowParams());
	}
	#endif
}


void LegacyTrackHandler::AddTrack(CUnit* unit, const float3& newPos)
{
	if (!unit->leaveTracks)
		return;

	const UnitDef* unitDef = unit->unitDef;
	const SolidObjectDecalDef& decalDef = unitDef->decalDef;

	if (!unitDef->IsGroundUnit())
		return;

	if (decalDef.trackDecalType < -1)
		return;

	if (decalDef.trackDecalType < 0) {
		const_cast<SolidObjectDecalDef&>(decalDef).trackDecalType = GetTrackType(decalDef.trackDecalTypeName);
		if (decalDef.trackDecalType < -1)
			return;
	}

	if (unit->myTrack != nullptr && unit->myTrack->lastUpdate >= (gs->frameNum - 7))
		return;

	if (!gu->spectatingFullView && (unit->losStatus[gu->myAllyTeam] & LOS_INLOS) == 0)
		return;

	// calculate typemap-index
	const int tmz = newPos.z / (SQUARE_SIZE * 2);
	const int tmx = newPos.x / (SQUARE_SIZE * 2);
	const int tmi = Clamp(tmz * mapDims.hmapx + tmx, 0, mapDims.hmapx * mapDims.hmapy - 1);

	const unsigned char* typeMap = readMap->GetTypeMapSynced();
	const CMapInfo::TerrainType& terType = mapInfo->terrainTypes[ typeMap[tmi] ];

	if (!terType.receiveTracks)
		return;

	static const int decalLevel = 3; //FIXME
	const float trackLifeTime = GAME_SPEED * decalLevel * decalDef.trackDecalStrength;

	if (trackLifeTime <= 0.0f)
		return;

	const float3 pos = newPos + unit->frontdir * decalDef.trackDecalOffset;


	// prepare the new part of the track; will be copied
	TrackPart trackPart;
	trackPart.pos1 = pos + unit->rightdir * decalDef.trackDecalWidth * 0.5f;
	trackPart.pos2 = pos - unit->rightdir * decalDef.trackDecalWidth * 0.5f;
	trackPart.pos1.y = CGround::GetHeightReal(trackPart.pos1.x, trackPart.pos1.z, false);
	trackPart.pos2.y = CGround::GetHeightReal(trackPart.pos2.x, trackPart.pos2.z, false);
	trackPart.creationTime = gs->frameNum;

	UnitTrackStruct** unitTrack = &unit->myTrack;

	if ((*unitTrack) == nullptr) {
		(*unitTrack) = new UnitTrackStruct(unit);
		(*unitTrack)->lifeTime = trackLifeTime;
		(*unitTrack)->alphaFalloff = 255.0f / trackLifeTime;

		trackPart.texPos = 0;
		trackPart.connected = false;
		trackPart.isNewTrack = true;
	} else {
		const TrackPart& prevPart = (*unitTrack)->lastAdded;

		const float partDist = (trackPart.pos1).distance(prevPart.pos1);
		const float texShift = (partDist / decalDef.trackDecalWidth) * decalDef.trackDecalStretch;

		trackPart.texPos = prevPart.texPos + texShift;
		trackPart.connected = (prevPart.creationTime == (gs->frameNum - 8));
	}

	if (trackPart.isNewTrack) {
		auto& decDef = unit->unitDef->decalDef;
		auto& trType = trackTypes[decDef.trackDecalType];

		spring::VectorInsertUnique(trType.tracks, *unitTrack);
	}

	(*unitTrack)->lastUpdate = gs->frameNum;
	(*unitTrack)->lastAdded = trackPart;

	tracksToBeAdded.push_back(*unitTrack);
}


int LegacyTrackHandler::GetTrackType(const std::string& name)
{
	const std::string& lowerName = StringToLower(name);

	const auto pred = [&](const TrackType& tt) { return (tt.name == lowerName); };
	const auto iter = std::find_if(trackTypes.begin(), trackTypes.end(), pred);

	if (iter != trackTypes.end())
		return (iter - trackTypes.begin());

	const GLuint texID = LoadTexture(lowerName);
	if (texID == 0)
		return -2;

	trackTypes.push_back(TrackType(lowerName, texID));
	return (trackTypes.size() - 1);
}


unsigned int LegacyTrackHandler::LoadTexture(const std::string& name)
{
	std::string fullName = name;
	if (fullName.find_first_of('.') == std::string::npos)
		fullName += ".bmp";

	if ((fullName.find_first_of('\\') == std::string::npos) &&
	    (fullName.find_first_of('/')  == std::string::npos)) {
		fullName = std::string("bitmaps/tracks/") + fullName;
	}

	CBitmap bm;
	if (!bm.Load(fullName)) {
		LOG_L(L_WARNING, "Could not load track decal from file %s", fullName.c_str());
		return 0;
	}
	if (FileSystem::GetExtension(fullName) == "bmp") {
		// bitmaps don't have an alpha channel
		// so use: red := brightness & green := alpha
		const unsigned char* rmem = bm.GetRawMem();
		      unsigned char* wmem = bm.GetRawMem();

		for (int y = 0; y < bm.ysize; ++y) {
			for (int x = 0; x < bm.xsize; ++x) {
				const int index = ((y * bm.xsize) + x) * 4;
				const int brightness = rmem[index + 0];

				wmem[index + 3] = rmem[index + 1];
				wmem[index + 0] = (brightness * 90) / 255;
				wmem[index + 1] = (brightness * 60) / 255;
				wmem[index + 2] = (brightness * 30) / 255;
			}
		}
	}

	return bm.CreateMipMapTexture();
}




void LegacyTrackHandler::RemoveTrack(CUnit* unit)
{
	if (unit->myTrack == nullptr)
		return;

	// same pointer as in tracksToBeAdded, so this also pre-empts DrawTracks
	unit->myTrack->owner = nullptr;
	unit->myTrack = nullptr;
}


void LegacyTrackHandler::UnitMoved(const CUnit* unit)
{
	AddTrack(const_cast<CUnit*>(unit), unit->pos);
}


void LegacyTrackHandler::RenderUnitDestroyed(const CUnit* unit)
{
	RemoveTrack(const_cast<CUnit*>(unit));
}

