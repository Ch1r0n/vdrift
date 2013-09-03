/************************************************************************/
/*                                                                      */
/* This file is part of VDrift.                                         */
/*                                                                      */
/* VDrift is free software: you can redistribute it and/or modify       */
/* it under the terms of the GNU General Public License as published by */
/* the Free Software Foundation, either version 3 of the License, or    */
/* (at your option) any later version.                                  */
/*                                                                      */
/* VDrift is distributed in the hope that it will be useful,            */
/* but WITHOUT ANY WARRANTY; without even the implied warranty of       */
/* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the        */
/* GNU General Public License for more details.                         */
/*                                                                      */
/* You should have received a copy of the GNU General Public License    */
/* along with VDrift.  If not, see <http://www.gnu.org/licenses/>.      */
/*                                                                      */
/************************************************************************/

#include "model_joe03.h"
#include "joepack.h"
#include "mathvector.h"
#include "endian_utility.h"

#include <vector>
using std::vector;

const int ModelJoe03::JOE_MAX_FACES = 32000;
const int ModelJoe03::JOE_VERSION = 3;
const float ModelJoe03::MODEL_SCALE = 1.0;

// This holds the header information that is read in at the beginning of the file
struct JoeHeader
{
	int magic;                   // This is used to identify the file
	int version;                 // The version number of the file
	int num_faces;	            // The number of faces (polygons)
	int num_frames;               // The number of animation frames
};

// This is used to store the vertices that are read in for the current frame
struct JoeVertex
{
	float vertex[3];
};

// This stores the indices into the vertex and texture coordinate arrays
struct JoeFace
{
	short vertexIndex[3];
	short normalIndex[3];
	short textureIndex[3];
};

// This stores UV coordinates
struct JoeTexCoord
{
	float u, v;
};

// This stores the frames vertices after they have been transformed
struct JoeFrame
{
	int num_verts;
	int num_texcoords;
	int num_normals;

	std::vector<JoeFace> faces;
	std::vector<JoeVertex> verts;
	std::vector<JoeVertex> normals;
	std::vector<JoeTexCoord> texcoords;
};

// This holds all the information for our model/scene.
struct JoeObject
{
	JoeHeader info;
	std::vector<JoeFrame> frames;
};

struct VertEntry
{
	VertEntry() : original_index(-1), norm_index(-1), tex_index(-1) {}
	int original_index;
	int norm_index;
	int tex_index;
};

static void CorrectEndian(std::vector<JoeFace> & p)
{
	for (unsigned int i = 0; i < p.size(); ++i)
	{
		for (int d = 0; d < 3; ++d)
		{
			p[i].vertexIndex[d] = ENDIAN_SWAP_16 ( p[i].vertexIndex[d] );
			p[i].normalIndex[d] = ENDIAN_SWAP_16 ( p[i].normalIndex[d] );
			p[i].textureIndex[d] = ENDIAN_SWAP_16 ( p[i].textureIndex[d] );
		}
	}
}

static void CorrectEndian(std::vector<JoeVertex> & p)
{
	for (unsigned int i = 0; i < p.size(); ++i)
	{
		for (int d = 0; d < 3; ++d)
		{
			p[i].vertex[d] = ENDIAN_SWAP_FLOAT ( p[i].vertex[d] );
		}
	}
}

static void CorrectEndian(std::vector<JoeTexCoord> & p)
{
	for (unsigned int i = 0; i < p.size(); ++i)
	{
		p[i].u = ENDIAN_SWAP_FLOAT ( p[i].u );
		p[i].v = ENDIAN_SWAP_FLOAT ( p[i].v );
	}
}

static int BinaryRead ( void * buffer, unsigned int size, unsigned int count, FILE * f, const JoePack * pack )
{
	unsigned int bytesread = 0;

	if ( pack == NULL )
	{
		bytesread = fread ( buffer, size, count, f );
	}
	else
	{
		bytesread = pack->fread ( buffer, size, count );
	}

	assert(bytesread == count);

	return bytesread;
}

///fix invalid normals (my own fault, i suspect.  the DOF converter i wrote may have flipped Y & Z normals)
static bool NeedsNormalSwap(JoeObject & Object)
{
	bool need_normal_flip = false;
	for (int f = 0; f < Object.info.num_frames; f++)
	{
		int normal_flip_count = 0;
		for (int i = 0; i < Object.info.num_faces; i++)
		{
			Vec3 tri[3];
			Vec3 norms[3];
			for (unsigned int v = 0; v < 3; v++)
			{
				assert(Object.frames[f].faces[i].vertexIndex[v] < Object.frames[f].num_verts);
				assert(Object.frames[f].faces[i].normalIndex[v] < Object.frames[f].num_normals);
				tri[v].Set(Object.frames[f].verts[Object.frames[f].faces[i].vertexIndex[v]].vertex);
				norms[v].Set(Object.frames[f].normals[Object.frames[f].faces[i].normalIndex[v]].vertex);
			}
			Vec3 norm;
			for (unsigned int v = 0; v < 3; v++)
				norm = norm + norms[v];
			Vec3 tnorm = (tri[2] - tri[0]).cross(tri[1] - tri[0]);
			if (tnorm.Magnitude() > 0.0001 && norm.Magnitude() > 0.0001)
			{
				norm = norm.Normalize();
				tnorm = tnorm.Normalize();
				if (norm.dot(tnorm) < 0.5 && norm.dot(tnorm) > -0.5)
				{
					normal_flip_count++;
					//std::cout << norm.dot(tnorm) << std::endl;
					//std::cout << norm << " -- " << tnorm << std::endl;
				}
			}
		}

		if (normal_flip_count > Object.info.num_faces/4)
			need_normal_flip = true;
	}
	return need_normal_flip;
}

bool ModelJoe03::Load ( const std::string & filename, std::ostream & err_output, bool genlist, const JoePack * pack)
{
	Clear();

	FILE * m_FilePointer = NULL;

	//open file
	if ( pack == NULL )
	{
		m_FilePointer = fopen(filename.c_str(), "rb");
		if (!m_FilePointer)
		{
			err_output << "MODEL_JOE03: Failed to open file " << filename << std::endl;
			return false;
		}
	}
	else
	{
		if (!pack->fopen(filename))
		{
			err_output << "MODEL_JOE03: Failed to open file " << filename << " in " << pack->GetPath() << std::endl;
			return false;
		}
	}

	bool val = LoadFromHandle ( m_FilePointer, pack, err_output );

	// Clean up after everything
	if ( pack == NULL )
		fclose ( m_FilePointer );
	else
		pack->fclose();

	if (val)
	{
		if (genlist)
		{
			//optimize into a static display list
			GenerateListID(err_output);
		}
		else
		{
			//optimize into vertex array/buffers
			GenerateVertexArrayObject(err_output);
		}
	}
	else
	{
		err_output << "in " << filename << std::endl;
	}

	return val;
}

bool ModelJoe03::LoadFromHandle ( FILE * m_FilePointer, const JoePack * pack, std::ostream & err_output )
{
	JoeObject Object;

	// Read the header data and store it in our variable
	BinaryRead ( &Object.info, sizeof ( JoeHeader ), 1, m_FilePointer, pack );

	Object.info.magic = ENDIAN_SWAP_32 ( Object.info.magic );
	Object.info.version = ENDIAN_SWAP_32 ( Object.info.version );
	Object.info.num_faces = ENDIAN_SWAP_32 ( Object.info.num_faces );
	Object.info.num_frames = ENDIAN_SWAP_32 ( Object.info.num_frames );

	// Make sure the version is what we expect or else it's a bad egg
	if ( Object.info.version != JOE_VERSION )
	{
		// Display an error message for bad file format, then stop loading
		err_output << "Invalid file format (Version is " << Object.info.version << " not " << JOE_VERSION << "). ";
		return false;
	}

	if ( Object.info.num_faces > JOE_MAX_FACES )
	{
		err_output << Object.info.num_faces << " faces (max " << JOE_MAX_FACES << "). ";
		return false;
	}

	// Read in the model data
	ReadData ( m_FilePointer, pack, Object );

	//generate metrics such as bounding box, etc
	GenerateMeshMetrics();

	// Return a success
	return true;
}

void ModelJoe03::ReadData ( FILE * m_FilePointer, const JoePack * pack, JoeObject & Object )
{
	int num_frames = Object.info.num_frames;
	int num_faces = Object.info.num_faces;

	Object.frames.resize(num_frames);

	for ( int i = 0; i < num_frames; i++ )
	{
		Object.frames[i].faces.resize(num_faces);

		BinaryRead ( &Object.frames[i].faces[0], sizeof ( JoeFace ), num_faces, m_FilePointer, pack );
		CorrectEndian ( Object.frames[i].faces );

		BinaryRead ( &Object.frames[i].num_verts, sizeof ( int ), 1, m_FilePointer, pack );
		Object.frames[i].num_verts = ENDIAN_SWAP_32 ( Object.frames[i].num_verts );
		BinaryRead ( &Object.frames[i].num_texcoords, sizeof ( int ), 1, m_FilePointer, pack );
		Object.frames[i].num_texcoords = ENDIAN_SWAP_32 ( Object.frames[i].num_texcoords );
		BinaryRead ( &Object.frames[i].num_normals, sizeof ( int ), 1, m_FilePointer, pack );
		Object.frames[i].num_normals = ENDIAN_SWAP_32 ( Object.frames[i].num_normals );

		Object.frames[i].verts.resize(Object.frames[i].num_verts);
		Object.frames[i].normals.resize(Object.frames[i].num_normals);
		Object.frames[i].texcoords.resize(Object.frames[i].num_texcoords);

		BinaryRead ( &Object.frames[i].verts[0], sizeof ( JoeVertex ), Object.frames[i].num_verts, m_FilePointer, pack );
		CorrectEndian ( Object.frames[i].verts );
		BinaryRead ( &Object.frames[i].normals[0], sizeof ( JoeVertex ), Object.frames[i].num_normals, m_FilePointer, pack );
		CorrectEndian ( Object.frames[i].normals );
		BinaryRead ( &Object.frames[i].texcoords[0], sizeof ( JoeTexCoord ), Object.frames[i].num_texcoords, m_FilePointer, pack );
		CorrectEndian ( Object.frames[i].texcoords );
	}

	//cout << "!!! loading " << modelpath << endl;

	//go do scaling
	for (int i = 0; i < num_frames; i++)
	{
		for ( int v = 0; v < Object.frames[i].num_verts; v++ )
		{
			Vec3 temp;

			temp.Set ( Object.frames[i].verts[v].vertex );
			temp = temp * MODEL_SCALE;

			for (int n = 0; n < 3; n++)
				Object.frames[i].verts[v].vertex[n] = temp[n];
		}
	}

	if (NeedsNormalSwap(Object))
	{
		for (int i = 0; i < num_frames; i++)
		{
			for ( int v = 0; v < Object.frames[i].num_normals; v++ )
			{
				std::swap(Object.frames[i].normals[v].vertex[1],
					  Object.frames[i].normals[v].vertex[2]);
				Object.frames[i].normals[v].vertex[1] = -Object.frames[i].normals[v].vertex[1];
			}
		}
		//std::cout << "!!! swapped normals !!!" << std::endl;
	}

	//assert(!NeedsNormalFlip(pObject));

	/*//make sure vertex ordering is consistent with normals
	for (i = 0; i < Object.info.num_faces; i++)
	{
		short vi[3];
		VERTEX tri[3];
		VERTEX norms[3];
		for (unsigned int v = 0; v < 3; v++)
		{
			vi[v] = GetFace(i)[v];
			tri[v].Set(GetVert(vi[v]));
			norms[v].Set(GetNorm(GetNormIdx(i)[v]));
		}
		VERTEX norm;
		for (unsigned int v = 0; v < 3; v++)
			norm = norm + norms[v];
		norm = norm.normalize();
		VERTEX tnorm = (tri[2] - tri[0]).cross(tri[1] - tri[0]);
		if (norm.dot(tnorm) > 0)
		{
			short tvi = Object.frames[0].faces[i].vertexIndex[1];
			Object.frames[0].faces[i].vertexIndex[1] = Object.frames[0].faces[i].vertexIndex[2];
			Object.frames[0].faces[i].vertexIndex[2] = tvi;

			tvi = Object.frames[0].faces[i].normalIndex[1];
			Object.frames[0].faces[i].normalIndex[1] = Object.frames[0].faces[i].normalIndex[2];
			Object.frames[0].faces[i].normalIndex[2] = tvi;

			tvi = Object.frames[0].faces[i].textureIndex[1];
			Object.frames[0].faces[i].textureIndex[1] = Object.frames[0].faces[i].textureIndex[2];
			Object.frames[0].faces[i].textureIndex[2] = tvi;
		}
	}*/

	//build unique vertices
	//cout << "building unique vertices...." << endl;
	int frame(0);

	typedef size_t size_type;

	vector <VertEntry> vert_master ((size_type)(Object.frames[frame].num_verts));
	vert_master.reserve(Object.frames[frame].num_verts*2);

	vector <int> v_faces((size_type)(Object.info.num_faces*3));

	for (int i = 0; i < Object.info.num_faces; i++)
	{
		for (int v = 0; v < 3; v++)
		{
			VertEntry & ve = vert_master[Object.frames[frame].faces[i].vertexIndex[v]];
			if (ve.original_index == -1) //first entry
			{
				ve.original_index = Object.frames[frame].faces[i].vertexIndex[v];
				ve.norm_index = Object.frames[frame].faces[i].normalIndex[v];
				ve.tex_index = Object.frames[frame].faces[i].textureIndex[v];
				//if (ve.tex_index < 0)
				assert(ve.tex_index >= 0);

				v_faces[i*3+v] = Object.frames[frame].faces[i].vertexIndex[v];
				//cout << "(first) face " << i << " vert " << v << " index: " << v_faces[i*3+v] << endl;

				//cout << "first entry: " << ve.original_index << "," << ve.norm_index << "," << ve.tex_index << endl;
			}
			else
			{
				//see if we match the pre-existing entry
				if (ve.norm_index == Object.frames[frame].faces[i].normalIndex[v] &&
					ve.tex_index == Object.frames[frame].faces[i].textureIndex[v])
				{
					v_faces[i*3+v] = Object.frames[frame].faces[i].vertexIndex[v];
					assert(ve.tex_index >= 0);
					//cout << "(matched) face " << i << " vert " << v << " index: " << v_faces[i*3+v] << endl;

					//cout << "matched entry: " << ve.original_index << "," << ve.norm_index << "," << ve.tex_index << endl;
				}
				else
				{
					//create a new entry
					vert_master.push_back(VertEntry());
					vert_master.back().original_index = Object.frames[frame].faces[i].vertexIndex[v];
					vert_master.back().norm_index = Object.frames[frame].faces[i].normalIndex[v];
					vert_master.back().tex_index = Object.frames[frame].faces[i].textureIndex[v];

					assert(vert_master.back().tex_index >= 0);

					v_faces[i*3+v] = vert_master.size()-1;
					//cout << "(new) face " << i << " vert " << v << " index: " << v_faces[i*3+v] << endl;

					//cout << "new entry: " << vert_master.back().original_index << "," << vert_master.back().norm_index << "," << vert_master.back().tex_index << " (" << ve.original_index << "," << ve.norm_index << "," << ve.tex_index << ")" << endl;
				}
			}
		}
	}

	/*for (int i = 0; i < vert_master.size(); i++)
	{
		if (vert_master[i].original_index < 0)
			std::cout << i << ", " << Object.frames[frame].num_verts << ", " << vert_master.size() << std::endl;
		assert(vert_master[i].original_index >= 0);
	}*/

	float newvertnum = vert_master.size();
	/*std::cout << modelpath << " (" << Object.info.num_faces << ") used to have " << Object.frames[frame].num_verts << " vertices, " <<
			Object.frames[frame].num_normals << " normals, " << Object.frames[frame].num_texcoords
			<< " tex coords, now it has " << newvertnum << " combo verts (combo indices)" << std::endl;*/

	//now, fill up the vertices, normals, and texcoords
	vector <float> v_vertices((size_type)(newvertnum*3));
	vector <float> v_texcoords((size_type)(newvertnum*2));
	vector <float> v_normals((size_type)(newvertnum*3));
	for (int i = 0; i < newvertnum; ++i)
	{
		if (vert_master[i].original_index >= 0)
		{
			for (int d = 0; d < 3; d++)
				v_vertices[i*3+d] = Object.frames[frame].verts[vert_master[i].original_index].vertex[d];

			for (int d = 0; d < 3; d++)
				v_normals[i*3+d] = Object.frames[frame].normals[vert_master[i].norm_index].vertex[d];

			//std::cout << i << ", " << vert_master[i].tex_index << ", " << vert_master.size() << ", " << Object.frames[frame].num_texcoords << std::endl;
			//assert(vert_master[i].tex_index >= 0);
			//assert(vert_master[i].tex_index < Object.frames[frame].num_texcoords);
			if (vert_master[i].tex_index < Object.frames[frame].num_texcoords)
			{
				v_texcoords[i*2+0] = Object.frames[frame].texcoords[vert_master[i].tex_index].u;
				v_texcoords[i*2+1] = Object.frames[frame].texcoords[vert_master[i].tex_index].v;
			}
			else
			{
				v_texcoords[i*2+0] = 0;
				v_texcoords[i*2+1] = 0;
			}
		}
	}

	/*for (int i = 0; i < newvertnum; i++)
		cout << v_vertices[i*3] << "," << v_vertices[i*3+1] << "," << v_vertices[i*3+2] << endl;*/

	//assign to our mesh
	m_mesh.SetFaces(&v_faces[0], v_faces.size());
	m_mesh.SetVertices(&v_vertices[0], v_vertices.size());
	m_mesh.SetNormals(&v_normals[0], v_normals.size());
	m_mesh.SetTexCoordSets(1);
	m_mesh.SetTexCoords(0, &v_texcoords[0], v_texcoords.size());
}

