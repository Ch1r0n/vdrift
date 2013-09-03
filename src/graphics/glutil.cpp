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

#include "glutil.h"
#include "glew.h"
#include <ostream>

bool CheckForOpenGLErrors(
	const std::string & activity_description,
	std::ostream & error_output)
{
	GLenum error = glGetError();
	if (error != GL_NO_ERROR)
	{
		const GLubyte *err_string = gluErrorString(error);
		error_output << "OpenGL error \"" << err_string << "\" during: " << activity_description << std::endl;
		return true;
	}
	return false;
}
