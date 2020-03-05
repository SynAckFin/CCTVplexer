/* 
    Copyright (C) 2020  Terry Sanders

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/
#ifndef _RENDERER_INCLUDED_
#define _RENDERER_INCLUDED_

int  RenderInitialise(void);
void RenderDeInitialise(void);
void *RenderNew(char *);
void *RenderGetBuffer(void *,int32_t *);
void *RenderProcessBuffer(void *,void *,int32_t,int32_t);
void RenderSetViewPort(void *,int,int,int,int,int,int,int,int);
void RendererSetInvisible(void *);
void RendererSetFullScreen(void *,int,int,double);
void RendererSetRectangle(void *,double,double,double,double,int,int,double);
void *RenderSetBackground(void *, uint32_t );
void RenderRelease(void *);

#endif
