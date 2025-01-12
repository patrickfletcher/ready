/*  Copyright 2011-2019 The Ready Bunch

    This file is part of Ready.

    Ready is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    Ready is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Ready. If not, see <http://www.gnu.org/licenses/>.         */

// local:
#include "FormulaOpenCLImageRD.hpp"
#include "utils.hpp"

// STL:
#include <string>
#include <sstream>
using namespace std;

// VTK:
#include <vtkXMLUtilities.h>

// -------------------------------------------------------------------------

FormulaOpenCLImageRD::FormulaOpenCLImageRD(int opencl_platform,int opencl_device,int data_type)
    : OpenCLImageRD(opencl_platform,opencl_device,data_type)
{
    // these settings are used in File > New Pattern
    this->SetRuleName("Gray-Scott");
    this->AddParameter("timestep",1.0f);
    this->AddParameter("D_a",0.082f);
    this->AddParameter("D_b",0.041f);
    this->AddParameter("K",0.06f);
    this->AddParameter("F",0.035f);
    this->SetFormula("\
delta_a = D_a * laplacian_a - a*b*b + F*(1.0"+this->data_type_suffix+"-a);\n\
delta_b = D_b * laplacian_b + a*b*b - (F+K)*b;");
}

// -------------------------------------------------------------------------

std::string FormulaOpenCLImageRD::AssembleKernelSourceFromFormula(std::string formula) const
{
    const string indent = "    ";
    const int NC = this->GetNumberOfChemicals();

    ostringstream kernel_source;
    kernel_source << fixed << setprecision(6);
    if( this->data_type == VTK_DOUBLE ) {
        kernel_source << "\
#ifdef cl_khr_fp64\n\
    #pragma OPENCL EXTENSION cl_khr_fp64 : enable\n\
#elif defined(cl_amd_fp64)\n\
    #pragma OPENCL EXTENSION cl_amd_fp64 : enable\n\
#else\n\
    #error \"Double precision floating point not supported on this OpenCL device. Choose another or contact the Ready team.\"\n\
#endif\n\n";
    }
    // output the function definition
    kernel_source << "__kernel void rd_compute(";
    for(int i=0;i<NC;i++)
        kernel_source << "__global " << this->data_type_string << "4 *" << GetChemicalName(i) << "_in,";
    for(int i=0;i<NC;i++)
    {
        kernel_source << "__global " << this->data_type_string << "4 *" << GetChemicalName(i) << "_out";
        if(i<NC-1)
            kernel_source << ",";
    }
    // output the first part of the body
    kernel_source << ")\n{\n" <<
        indent << "const int index_x = get_global_id(0);\n" <<
        indent << "const int index_y = get_global_id(1);\n" <<
        indent << "const int index_z = get_global_id(2);\n" <<
        indent << "const int X = get_global_size(0);\n" <<
        indent << "const int Y = get_global_size(1);\n" <<
        indent << "const int Z = get_global_size(2);\n" <<
        indent << "const int index_here = X*(Y*index_z + index_y) + index_x;\n\n";
    for(int i=0;i<NC;i++)
        kernel_source << indent << this->data_type_string << "4 " << GetChemicalName(i) << " = " << GetChemicalName(i) << "_in[index_here];\n"; // "float4 a = a_in[index_here];"
    if(this->neighborhood_type==FACE_NEIGHBORS && this->GetArenaDimensionality()==3 && this->neighborhood_range==1) // neighborhood_weight not relevant
    {
        const int NDIRS = 6;
        const string dir[NDIRS]={"left","right","up","down","fore","back"};
        // output the Laplacian part of the body
        kernel_source << "\n" << indent << "// compute the Laplacians of each chemical\n";
        kernel_source << indent << "// 3D 7-point stencil: [ [ 0,0,0; 0,1,0; 0,0,0 ], [0,1,0; 1,-6,1; 0,1,0 ], [ 0,0,0; 0,1,0; 0,0,0 ] ]\n";
        if(this->wrap)
            kernel_source <<
                indent << "const int xm1 = (index_x-1+X) & (X-1); // wrap (assumes X is a power of 2)\n" <<
                indent << "const int xp1 = (index_x+1) & (X-1);\n" <<
                indent << "const int ym1 = (index_y-1+Y) & (Y-1);\n" <<
                indent << "const int yp1 = (index_y+1) & (Y-1);\n" <<
                indent << "const int zm1 = (index_z-1+Z) & (Z-1);\n" <<
                indent << "const int zp1 = (index_z+1) & (Z-1);\n";
        else
            kernel_source <<
                indent << "const int xm1 = max(0,index_x-1);\n" <<
                indent << "const int ym1 = max(0,index_y-1);\n" <<
                indent << "const int zm1 = max(0,index_z-1);\n" <<
                indent << "const int xp1 = min(X-1,index_x+1);\n" <<
                indent << "const int yp1 = min(Y-1,index_y+1);\n" <<
                indent << "const int zp1 = min(Z-1,index_z+1);\n";
        kernel_source <<
            indent << "const int index_left =  X*(Y*index_z + index_y) + xm1;\n" <<
            indent << "const int index_right = X*(Y*index_z + index_y) + xp1;\n" <<
            indent << "const int index_up =    X*(Y*index_z + ym1) + index_x;\n" <<
            indent << "const int index_down =  X*(Y*index_z + yp1) + index_x;\n" <<
            indent << "const int index_fore =  X*(Y*zm1 + index_y) + index_x;\n" <<
            indent << "const int index_back =  X*(Y*zp1 + index_y) + index_x;\n";
        for(int iC=0;iC<NC;iC++)
            for(int iDir=0;iDir<NDIRS;iDir++)
                kernel_source << indent << this->data_type_string << "4 " << GetChemicalName(iC) << "_" << dir[iDir] << " = " << GetChemicalName(iC) << "_in[index_" << dir[iDir] << "];\n";
        kernel_source << indent << "const " << this->data_type_string << "4 _K0 = -6.0" << this->data_type_suffix << "; // center weight\n";
        for(int iC=0;iC<NC;iC++)
        {
            string chem = GetChemicalName(iC);
            kernel_source << indent << this->data_type_string << "4 laplacian_" << chem << " = (" << this->data_type_string << "4)(" <<
                chem << "_up.x + " << chem << ".y + " << chem << "_down.x + " << chem << "_left.w + " << chem << "_fore.x + " << chem << "_back.x,\n";
            kernel_source << indent <<
                chem << "_up.y + " << chem << ".z + " << chem << "_down.y + " << chem << ".x + " << chem << "_fore.y + " << chem << "_back.y,\n";
            kernel_source << indent <<
                chem << "_up.z + " << chem << ".w + " << chem << "_down.z + " << chem << ".y + " << chem << "_fore.z + " << chem << "_back.z,\n";
            kernel_source << indent <<
                chem << "_up.w + " << chem << "_right.x + " << chem << "_down.w + " << chem << ".z + " << chem << "_fore.w + " << chem << "_back.w) + _K0*" << chem << ";\n";
        }
        //                 (x y z w)                               up
        //       (x y z w) [x y z w] (x y z w)        =     left    .   right      (plus fore and back in the 3rd dimension)
        //                 (x y z w)                              down
    }
    else if(this->neighborhood_type==EDGE_NEIGHBORS && this->GetArenaDimensionality()==2 && this->neighborhood_range==1) // neighborhood_weight not relevant
    {
        const int NDIRS = 4;
        const string dir[NDIRS]={"left","right","up","down"};
        // output the Laplacian part of the body
        kernel_source << "\n" << indent << "// compute the Laplacians of each chemical\n";
        kernel_source << indent << "// 2D 5-point stencil: [ 0,1,0; 1,-4,1; 0,1,0 ]\n";
        if(this->wrap)
            kernel_source <<
                indent << "const int xm1 = (index_x-1+X) & (X-1); // wrap (assumes X is a power of 2)\n" <<
                indent << "const int xp1 = (index_x+1) & (X-1);\n" <<
                indent << "const int ym1 = (index_y-1+Y) & (Y-1);\n" <<
                indent << "const int yp1 = (index_y+1) & (Y-1);\n";
        else
            kernel_source <<
                indent << "const int xm1 = max(0,index_x-1);\n" <<
                indent << "const int ym1 = max(0,index_y-1);\n" <<
                indent << "const int xp1 = min(X-1,index_x+1);\n" <<
                indent << "const int yp1 = min(Y-1,index_y+1);\n";
        kernel_source <<
            indent << "const int index_left =  X*(Y*index_z + index_y) + xm1;\n" <<
            indent << "const int index_right = X*(Y*index_z + index_y) + xp1;\n" <<
            indent << "const int index_up =    X*(Y*index_z + ym1) + index_x;\n" <<
            indent << "const int index_down =  X*(Y*index_z + yp1) + index_x;";
        for(int iC=0;iC<NC;iC++)
            for(int iDir=0;iDir<NDIRS;iDir++)
                kernel_source << indent << this->data_type_string << "4 " << GetChemicalName(iC) << "_" << dir[iDir] << " = " << GetChemicalName(iC) << "_in[index_" << dir[iDir] << "];\n";
        kernel_source << indent << "const " << this->data_type_string << "4 _K0 = -4.0" << this->data_type_suffix << "; // center weight\n";
        for(int iC=0;iC<NC;iC++)
        {
            string chem = GetChemicalName(iC);
            kernel_source << indent << this->data_type_string << "4 laplacian_" << chem << " = (" << this->data_type_string << "4)("
                << chem << "_up.x + " << chem << ".y + " << chem << "_down.x + " << chem << "_left.w,\n";
            kernel_source << indent <<
                chem << "_up.y + " << chem << ".z + " << chem << "_down.y + " << chem << ".x,\n";
            kernel_source << indent <<
                chem << "_up.z + " << chem << ".w + " << chem << "_down.z + " << chem << ".y,\n";
            kernel_source << indent <<
                chem << "_up.w + " << chem << "_right.x + " << chem << "_down.w + " << chem << ".z) + _K0*" << chem << ";\n";
        }
        //                 (x y z w)                               up
        //       (x y z w) [x y z w] (x y z w)        =     left    .   right
        //                 (x y z w)                              down
    }
    else if(this->GetArenaDimensionality()==1 && this->neighborhood_range==1) // neighborhood_type and neighborhood_weight not relevant
    {
        const int NDIRS = 2;
        const string dir[NDIRS]={"left","right"};
        // output the Laplacian part of the body
        kernel_source << "\n" << indent << "// compute the Laplacians of each chemical\n";
        kernel_source << indent << "// 1D 3-point stencil: [ 1,-2,1 ]\n";
        if(this->wrap)
            kernel_source <<
                indent << "const int xm1 = (index_x-1+X) & (X-1); // wrap (assumes X is a power of 2)\n" <<
                indent << "const int xp1 = (index_x+1) & (X-1);\n";
        else
            kernel_source <<
                indent << "const int xm1 = max(0,index_x-1);\n" <<
                indent << "const int xp1 = min(X-1,index_x+1);\n";
        kernel_source <<
            indent << "const int index_left =  X*(Y*index_z + index_y) + xm1;\n" <<
            indent << "const int index_right = X*(Y*index_z + index_y) + xp1;\n";
        for(int iC=0;iC<NC;iC++)
            for(int iDir=0;iDir<NDIRS;iDir++)
                kernel_source << indent << this->data_type_string << "4 " << GetChemicalName(iC) << "_" << dir[iDir] << " = " << GetChemicalName(iC) << "_in[index_" << dir[iDir] << "];\n";
        kernel_source << indent << "const " << this->data_type_string << "4 _K0 = -2.0" << this->data_type_suffix << "; // center weight\n";
        for(int iC=0;iC<NC;iC++)
        {
            string chem = GetChemicalName(iC);
            kernel_source << indent << this->data_type_string << "4 laplacian_" << chem << " = (" << this->data_type_string << "4)(" << chem << "_left.w + " << chem << ".y,\n";
            kernel_source << indent << chem << ".x + " << chem << ".z,\n";
            kernel_source << indent << chem << ".y + " << chem << ".w,\n";
            kernel_source << indent << chem << ".z + " << chem << "_right.x) + _K0*" << chem << ";\n";
        }
        //       (x y z w) [x y z w] (x y z w)        =     left    .   right
    }
    else if(this->neighborhood_type==VERTEX_NEIGHBORS && this->GetArenaDimensionality()==2 && this->neighborhood_range==1 && this->neighborhood_weight_type==LAPLACIAN)
    {
        const int NDIRS = 8;
        const string dir[NDIRS]={"n","ne","e","se","s","sw","w","nw"};
        // output the Laplacian part of the body
        kernel_source << "\n" << indent << "// compute the Laplacians of each chemical\n";
        kernel_source << indent << "// 2D standard 9-point stencil: [ 1,4,1; 4,-20,4; 1,4,1 ] / 6\n";
        if(this->wrap)
            kernel_source <<
                indent << "const int xm1 = (index_x-1+X) & (X-1); // wrap (assumes X is a power of 2)\n" <<
                indent << "const int xp1 = (index_x+1) & (X-1);\n" <<
                indent << "const int ym1 = (index_y-1+Y) & (Y-1);\n" <<
                indent << "const int yp1 = (index_y+1) & (Y-1);\n";
        else
            kernel_source <<
                indent << "const int xm1 = max(0,index_x-1);\n" <<
                indent << "const int ym1 = max(0,index_y-1);\n" <<
                indent << "const int xp1 = min(X-1,index_x+1);\n" <<
                indent << "const int yp1 = min(Y-1,index_y+1);\n";
        kernel_source <<
            indent << "const int index_n =  X*(Y*index_z + ym1) + index_x;\n" <<
            indent << "const int index_ne = X*(Y*index_z + ym1) + xp1;\n" <<
            indent << "const int index_e =  X*(Y*index_z + index_y) + xp1;\n" <<
            indent << "const int index_se = X*(Y*index_z + yp1) + xp1;\n" <<
            indent << "const int index_s =  X*(Y*index_z + yp1) + index_x;\n" <<
            indent << "const int index_sw = X*(Y*index_z + yp1) + xm1;\n" <<
            indent << "const int index_w =  X*(Y*index_z + index_y) + xm1;\n" <<
            indent << "const int index_nw = X*(Y*index_z + ym1) + xm1;\n";
        for(int iC=0;iC<NC;iC++)
            for(int iDir=0;iDir<NDIRS;iDir++)
                kernel_source << indent << this->data_type_string << "4 " << GetChemicalName(iC) << "_" << dir[iDir] << " = " << GetChemicalName(iC) << "_in[index_" << dir[iDir] << "];\n";
        kernel_source << indent << "const " << this->data_type_string << "4 _K0 = -20.0" << this->data_type_suffix << "/6.0" << this->data_type_suffix << "; // center weight\n";
        kernel_source << indent << "const " << this->data_type_string << " _K1 = 4.0" << this->data_type_suffix << "/6.0" << this->data_type_suffix << "; // edge-neighbors\n";
        kernel_source << indent << "const " << this->data_type_string << " _K2 = 1.0" << this->data_type_suffix << "/6.0" << this->data_type_suffix << "; // vertex-neighbors\n";
        for(int iC=0;iC<NC;iC++)
        {
            string chem = GetChemicalName(iC);
            kernel_source << indent << this->data_type_string << "4 laplacian_" << chem << " = (" << this->data_type_string << "4)(" <<
                chem << "_n.x*_K1 + " << chem << "_n.y*_K2 + " << chem << ".y*_K1 + " << chem << "_s.y*_K2 + "
                << chem << "_s.x*_K1 + " << chem << "_sw.w*_K2 + " << chem << "_w.w*_K1 + " << chem << "_nw.w*_K2,\n";
            kernel_source << indent <<
                chem << "_n.y*_K1 + " << chem << "_n.z*_K2 + " << chem << ".z*_K1 + " << chem << "_s.z*_K2 + "
                << chem << "_s.y*_K1 + " << chem << "_s.x*_K2 + " << chem << ".x*_K1 + " << chem << "_n.x*_K2,\n";
            kernel_source << indent <<
                chem << "_n.z*_K1 + " << chem << "_n.w*_K2 + " << chem << ".w*_K1 + " << chem << "_s.w*_K2 + "
                << chem << "_s.z*_K1 + " << chem << "_s.y*_K2 + " << chem << ".y*_K1 + " << chem << "_n.y*_K2,\n";
            kernel_source << indent <<
                chem << "_n.w*_K1 + " << chem << "_ne.x*_K2 + " << chem << "_e.x*_K1 + " << chem << "_se.x*_K2 + "
                << chem << "_s.w*_K1 + " << chem << "_s.z*_K2 + " << chem << ".z*_K1 + " << chem << "_n.z*_K2 ) + "
                << chem << "*_K0;\n";
        }
        //       (x y z w) (x y z w) (x y z w)           nw   n   ne
        //       (x y z w) [x y z w] (x y z w)    =       w   .   e
        //       (x y z w) (x y z w) (x y z w)           sw   s   se
    }
    else if(this->neighborhood_type==VERTEX_NEIGHBORS && this->GetArenaDimensionality()==2 && this->neighborhood_range==1 && this->neighborhood_weight_type==EQUAL)
    {
        const int NDIRS = 8;
        const string dir[NDIRS]={"n","ne","e","se","s","sw","w","nw"};
        // output the Laplacian part of the body
        kernel_source << "\n" << indent << "// compute the Laplacians of each chemical\n";
        kernel_source << indent << "// 2D equal-weighted 9-point stencil: [ 1,1,1; 1,-8,1; 1,1,1 ] / 2\n";
        if(this->wrap)
            kernel_source <<
                indent << "const int xm1 = (index_x-1+X) & (X-1); // wrap (assumes X is a power of 2)\n" <<
                indent << "const int xp1 = (index_x+1) & (X-1);\n" <<
                indent << "const int ym1 = (index_y-1+Y) & (Y-1);\n" <<
                indent << "const int yp1 = (index_y+1) & (Y-1);\n";
        else
            kernel_source <<
                indent << "const int xm1 = max(0,index_x-1);\n" <<
                indent << "const int ym1 = max(0,index_y-1);\n" <<
                indent << "const int xp1 = min(X-1,index_x+1);\n" <<
                indent << "const int yp1 = min(Y-1,index_y+1);\n";
        kernel_source <<
            indent << "const int index_n =  X*(Y*index_z + ym1) + index_x;\n" <<
            indent << "const int index_ne = X*(Y*index_z + ym1) + xp1;\n" <<
            indent << "const int index_e =  X*(Y*index_z + index_y) + xp1;\n" <<
            indent << "const int index_se = X*(Y*index_z + yp1) + xp1;\n" <<
            indent << "const int index_s =  X*(Y*index_z + yp1) + index_x;\n" <<
            indent << "const int index_sw = X*(Y*index_z + yp1) + xm1;\n" <<
            indent << "const int index_w =  X*(Y*index_z + index_y) + xm1;\n" <<
            indent << "const int index_nw = X*(Y*index_z + ym1) + xm1;\n";
        for(int iC=0;iC<NC;iC++)
            for(int iDir=0;iDir<NDIRS;iDir++)
                kernel_source << indent << this->data_type_string << "4 " << GetChemicalName(iC) << "_" << dir[iDir] << " = " << GetChemicalName(iC) << "_in[index_" << dir[iDir] << "];\n";
        kernel_source << indent << "const " << this->data_type_string << "4 _K0 = -4.0" << this->data_type_suffix << "; // center weight\n";
        kernel_source << indent << "const " << this->data_type_string << "4 _K1 = 1.0" << this->data_type_suffix << "/2.0" << this->data_type_suffix << "; // edge-neighbors\n";
        for(int iC=0;iC<NC;iC++)
        {
            string chem = GetChemicalName(iC);
            kernel_source << indent << this->data_type_string << "4 laplacian_" << chem << " = (" << this->data_type_string << "4)(" <<
                chem << "_n.x + " << chem << "_n.y + " << chem << ".y + " << chem << "_s.y + " << chem << "_s.x + " << chem << "_sw.w + " << chem << "_w.w + " << chem << "_nw.w,\n";
            kernel_source << indent <<
                chem << "_n.y + " << chem << "_n.z + " << chem << ".z + " << chem << "_s.z + " << chem << "_s.y + " << chem << "_s.x + " << chem << ".x + " << chem << "_n.x,\n";
            kernel_source << indent <<
                chem << "_n.z + " << chem << "_n.w + " << chem << ".w + " << chem << "_s.w + " << chem << "_s.z + " << chem << "_s.y + " << chem << ".y + " << chem << "_n.y,\n";
            kernel_source << indent <<
                chem << "_n.w + " << chem << "_ne.x + " << chem << "_e.x + " << chem << "_se.x + " << chem << "_s.w + " << chem << "_s.z + " << chem << ".z + " << chem << "_n.z" <<
                    ")*_K1 + _K0*" << chem << ";\n";
        }
        //       (x y z w) (x y z w) (x y z w)           nw   n   ne
        //       (x y z w) [x y z w] (x y z w)    =       w   .   e
        //       (x y z w) (x y z w) (x y z w)           sw   s   se
    }
    else if(this->neighborhood_type==EDGE_NEIGHBORS && this->GetArenaDimensionality()==3 && this->neighborhood_range==1 && this->neighborhood_weight_type==LAPLACIAN)
    {
        // 19-point stencil, following:
        // Dowle, Mantel & Barkley (1997) "Fast simulations of waves in three-dimensional excitable media"
        // Int. J. Bifurcation and Chaos, 7(11): 2529-2545.
        const int NDIRS = 18;
        const string dir[NDIRS]={"n","ne","e","se","s","sw","w","nw","d","dn","de","ds","dw","u","un","ue","us","uw"};
        // output the Laplacian part of the body
        kernel_source << "\n" << indent << "// compute the Laplacians of each chemical\n";
        kernel_source << indent << "// 3D 19-point stencil: [ [ 0,1,0; 1,2,1; 0,1,0 ], [ 1,2,1; 2,-24,2; 1,2,1 ], [ 0,1,0; 1,2,1; 0,1,0 ] ] / 6\n";
        if(this->wrap)
            kernel_source <<
                indent << "const int xm1 = (index_x-1+X) & (X-1); // wrap (assumes X is a power of 2)\n" <<
                indent << "const int xp1 = (index_x+1) & (X-1);\n" <<
                indent << "const int ym1 = (index_y-1+Y) & (Y-1);\n" <<
                indent << "const int yp1 = (index_y+1) & (Y-1);\n" <<
                indent << "const int zm1 = (index_z-1+Z) & (Z-1);\n" <<
                indent << "const int zp1 = (index_z+1) & (Z-1);\n";
        else
            kernel_source <<
                indent << "const int xm1 = max(0,index_x-1);\n" <<
                indent << "const int ym1 = max(0,index_y-1);\n" <<
                indent << "const int zm1 = max(0,index_z-1);\n" <<
                indent << "const int xp1 = min(X-1,index_x+1);\n" <<
                indent << "const int yp1 = min(Y-1,index_y+1);\n" <<
                indent << "const int zp1 = min(Z-1,index_z+1);\n";
        kernel_source <<
            indent << "const int index_n =  X*(Y*index_z + ym1) + index_x;\n" <<
            indent << "const int index_ne = X*(Y*index_z + ym1) + xp1;\n" <<
            indent << "const int index_e =  X*(Y*index_z + index_y) + xp1;\n" <<
            indent << "const int index_se = X*(Y*index_z + yp1) + xp1;\n" <<
            indent << "const int index_s =  X*(Y*index_z + yp1) + index_x;\n" <<
            indent << "const int index_sw = X*(Y*index_z + yp1) + xm1;\n" <<
            indent << "const int index_w =  X*(Y*index_z + index_y) + xm1;\n" <<
            indent << "const int index_nw = X*(Y*index_z + ym1) + xm1;\n" <<
            indent << "const int index_d =  X*(Y*zm1 + index_y) + index_x;\n" <<
            indent << "const int index_dn =  X*(Y*zm1 + ym1) + index_x;\n" <<
            indent << "const int index_de =  X*(Y*zm1 + index_y) + xp1;\n" <<
            indent << "const int index_ds =  X*(Y*zm1 + yp1) + index_x;\n" <<
            indent << "const int index_dw =  X*(Y*zm1 + index_y) + xm1;\n" <<
            indent << "const int index_u =  X*(Y*zp1 + index_y) + index_x;\n" <<
            indent << "const int index_un =  X*(Y*zp1 + ym1) + index_x;\n" <<
            indent << "const int index_ue =  X*(Y*zp1 + index_y) + xp1;\n" <<
            indent << "const int index_us =  X*(Y*zp1 + yp1) + index_x;\n" <<
            indent << "const int index_uw =  X*(Y*zp1 + index_y) + xm1;\n";
        for(int iC=0;iC<NC;iC++)
            for(int iDir=0;iDir<NDIRS;iDir++)
                kernel_source << indent << this->data_type_string << "4 " << GetChemicalName(iC) << "_" << dir[iDir] << " = " << GetChemicalName(iC) << "_in[index_" << dir[iDir] << "];\n";
        kernel_source << indent << "const " << this->data_type_string << "4 _K0 = -24.0" << this->data_type_suffix << "/6.0" << this->data_type_suffix << "; // center weight\n";
        kernel_source << indent << "const " << this->data_type_string << " _K1 = 2.0" << this->data_type_suffix << "/6.0" << this->data_type_suffix << "; // face-neighbors\n";
        kernel_source << indent << "const " << this->data_type_string << " _K2 = 1.0" << this->data_type_suffix << "/6.0" << this->data_type_suffix << "; // edge-neighbors\n";
        for(int iC=0;iC<NC;iC++)
        {
            string chem = GetChemicalName(iC);
            kernel_source << indent << this->data_type_string << "4 laplacian_" << chem << " = (" << this->data_type_string << "4)(\n";
            // x:
            // first collect the 6 face-neighbors:
            kernel_source << indent << indent << "(" << chem << "_d.x + " << chem << "_n.x + " << chem << ".y + " <<
                chem << "_s.x + " << chem << "_w.w + " << chem << "_u.x) * _K1 +\n";
            // then collect the 12 edge-neighbors
            kernel_source << indent << indent << "(" << chem << "_dn.x + " << chem << "_d.y + " << chem << "_ds.x + " << chem << "_dw.w + " <<
                chem << "_nw.w + " << chem << "_n.y + " << chem << "_s.y + " << chem << "_sw.w + " <<
                chem << "_un.x + " << chem << "_u.y + " << chem << "_us.x + " << chem << "_uw.w) * _K2 ,        // x\n";
            // y:
            // first collect the 6 face-neighbors:
            kernel_source << indent << indent << "(" << chem << "_d.y + " << chem << "_n.y + " << chem << ".z + " <<
                chem << "_s.y + " << chem << ".x + " << chem << "_u.y) * _K1 +\n";
            // then collect the 12 edge-neighbors
            kernel_source << indent << indent << "(" << chem << "_dn.y + " << chem << "_d.z + " << chem << "_ds.y + " << chem << "_d.x + " <<
                chem << "_n.x + " << chem << "_n.z + " << chem << "_s.z + " << chem << "_s.x + " <<
                chem << "_un.y + " << chem << "_u.z + " << chem << "_us.y + " << chem << "_u.x) * _K2 ,         // y\n";
            // z:
            // first collect the 6 face-neighbors:
            kernel_source << indent << indent << "(" << chem << "_d.z + " << chem << "_n.z + " << chem << ".w + " <<
                chem << "_s.z + " << chem << ".y + " << chem << "_u.z) * _K1 +\n";
            // then collect the 12 edge-neighbors
            kernel_source << indent << indent << "(" << chem << "_dn.z + " << chem << "_d.w + " << chem << "_ds.z + " << chem << "_d.y + " <<
                chem << "_n.y + " << chem << "_n.w + " << chem << "_s.w + " << chem << "_s.y + " <<
                chem << "_un.z + " << chem << "_u.w + " << chem << "_us.z + " << chem << "_u.y) * _K2 ,      // z\n";
            // w:
            // first collect the 6 face-neighbors:
            kernel_source << indent << indent << "(" << chem << "_d.w + " << chem << "_n.w + " << chem << "_e.x + " <<
                chem << "_s.w + " << chem << ".z + " << chem << "_u.w) * _K1 +\n";
            // then collect the 12 edge-neighbors
            kernel_source << indent << indent << "(" << chem << "_dn.w + " << chem << "_de.x + " << chem << "_ds.w + " << chem << "_d.z + " <<
                chem << "_n.z + " << chem << "_ne.x + " << chem << "_se.x + " << chem << "_s.z + " <<
                chem << "_un.w + " << chem << "_ue.x + " << chem << "_us.w + " << chem << "_u.z) * _K2 )    // w\n";
            // the final entry:
            kernel_source << indent << indent << " + " << chem << " * _K0;\n";
        }
        //              down                                                              up
        //            (x y z w)             (x y z w) (x y z w) (x y z w)             (x y z w)                        dn            nw   n   ne          un
        //  (x y z w) (x y z w) (x y z w)   (x y z w) [x y z w] (x y z w)   (x y z w) (x y z w) (x y z w)  =       dw   d   de        w   .   e       uw   u   ue
        //            (x y z w)             (x y z w) (x y z w) (x y z w)             (x y z w)                        ds            sw   s   se          us
    }
    else if(this->neighborhood_type==VERTEX_NEIGHBORS && this->GetArenaDimensionality()==3 && this->neighborhood_range==1 && this->neighborhood_weight_type==LAPLACIAN)
    {
        // 27-point stencil, following:
        // O'Reilly and Beck (2006) "A Family of Large-Stencil Discrete Laplacian Approximations in Three Dimensions"
        // Int. J. Num. Meth. Eng.
        // (Equation 22)
        const int NDIRS = 26;
        const string dir[NDIRS]={"n","ne","e","se","s","sw","w","nw","d","dn","dne","de","dse","ds","dsw","dw","dnw",
            "u","un","une","ue","use","us","usw","uw","unw"};
        // output the Laplacian part of the body
        kernel_source << "\n" << indent << "// compute the Laplacians of each chemical\n";
        kernel_source << indent << "// 3D 27-point stencil: [ [ 2,3,2; 3,6,3; 2,3,2 ], [ 3,6,3; 6,-88,6; 3,6,3 ], [ 2,3,2; 3,6,3; 2,3,2 ] ] / 26\n";
        if(this->wrap)
            kernel_source <<
                indent << "const int xm1 = (index_x-1+X) & (X-1); // wrap (assumes X is a power of 2)\n" <<
                indent << "const int xp1 = (index_x+1) & (X-1);\n" <<
                indent << "const int ym1 = (index_y-1+Y) & (Y-1);\n" <<
                indent << "const int yp1 = (index_y+1) & (Y-1);\n" <<
                indent << "const int zm1 = (index_z-1+Z) & (Z-1);\n" <<
                indent << "const int zp1 = (index_z+1) & (Z-1);\n";
        else
            kernel_source <<
                indent << "const int xm1 = max(0,index_x-1);\n" <<
                indent << "const int ym1 = max(0,index_y-1);\n" <<
                indent << "const int zm1 = max(0,index_z-1);\n" <<
                indent << "const int xp1 = min(X-1,index_x+1);\n" <<
                indent << "const int yp1 = min(Y-1,index_y+1);\n" <<
                indent << "const int zp1 = min(Z-1,index_z+1);\n";
        kernel_source <<
            indent << "const int index_n =  X*(Y*index_z + ym1) + index_x;\n" <<
            indent << "const int index_ne = X*(Y*index_z + ym1) + xp1;\n" <<
            indent << "const int index_e =  X*(Y*index_z + index_y) + xp1;\n" <<
            indent << "const int index_se = X*(Y*index_z + yp1) + xp1;\n" <<
            indent << "const int index_s =  X*(Y*index_z + yp1) + index_x;\n" <<
            indent << "const int index_sw = X*(Y*index_z + yp1) + xm1;\n" <<
            indent << "const int index_w =  X*(Y*index_z + index_y) + xm1;\n" <<
            indent << "const int index_nw = X*(Y*index_z + ym1) + xm1;\n" <<
            indent << "const int index_d =  X*(Y*zm1 + index_y) + index_x;\n" <<
            indent << "const int index_dn =  X*(Y*zm1 + ym1) + index_x;\n" <<
            indent << "const int index_dne = X*(Y*zm1 + ym1) + xp1;\n" <<
            indent << "const int index_de =  X*(Y*zm1 + index_y) + xp1;\n" <<
            indent << "const int index_dse = X*(Y*zm1 + yp1) + xp1;\n" <<
            indent << "const int index_ds =  X*(Y*zm1 + yp1) + index_x;\n" <<
            indent << "const int index_dsw = X*(Y*zm1 + yp1) + xm1;\n" <<
            indent << "const int index_dw =  X*(Y*zm1 + index_y) + xm1;\n" <<
            indent << "const int index_dnw = X*(Y*zm1 + ym1) + xm1;\n" <<
            indent << "const int index_u =  X*(Y*zp1 + index_y) + index_x;\n" <<
            indent << "const int index_un =  X*(Y*zp1 + ym1) + index_x;\n" <<
            indent << "const int index_une = X*(Y*zp1 + ym1) + xp1;\n" <<
            indent << "const int index_ue =  X*(Y*zp1 + index_y) + xp1;\n" <<
            indent << "const int index_use = X*(Y*zp1 + yp1) + xp1;\n" <<
            indent << "const int index_us =  X*(Y*zp1 + yp1) + index_x;\n" <<
            indent << "const int index_usw = X*(Y*zp1 + yp1) + xm1;\n" <<
            indent << "const int index_uw =  X*(Y*zp1 + index_y) + xm1;\n" <<
            indent << "const int index_unw = X*(Y*zp1 + ym1) + xm1;\n";
        for(int iC=0;iC<NC;iC++)
            for(int iDir=0;iDir<NDIRS;iDir++)
                kernel_source << indent << this->data_type_string << "4 " << GetChemicalName(iC) << "_" << dir[iDir] << " = " << GetChemicalName(iC) << "_in[index_" << dir[iDir] << "];\n";
        kernel_source << indent << "const " << this->data_type_string << "4 _K0 = -88.0" << this->data_type_suffix << "/26.0" << this->data_type_suffix << "; // center weight\n";
        kernel_source << indent << "const " << this->data_type_string << " _K1 = 6.0" << this->data_type_suffix << "/26.0" << this->data_type_suffix << "; // face-neighbors\n";
        kernel_source << indent << "const " << this->data_type_string << " _K2 = 3.0" << this->data_type_suffix << "/26.0" << this->data_type_suffix << "; // edge-neighbors\n";
        kernel_source << indent << "const " << this->data_type_string << " _K3 = 2.0" << this->data_type_suffix << "/26.0" << this->data_type_suffix << "; // corner-neighbors\n";
        for(int iC=0;iC<NC;iC++)
        {
            string chem = GetChemicalName(iC);
            kernel_source << indent << this->data_type_string << "4 laplacian_" << chem << " = (" << this->data_type_string << "4)(\n";
            // x:
            // first collect the 6 face-neighbors:
            kernel_source << indent << indent << "(" << chem << "_d.x + " << chem << "_n.x + " << chem << ".y + " <<
                chem << "_s.x + " << chem << "_w.w + " << chem << "_u.x) * _K1 +\n";
            // then collect the 12 edge-neighbors
            kernel_source << indent << indent << "(" << chem << "_dn.x + " << chem << "_d.y + " << chem << "_ds.x + " << chem << "_dw.w + " <<
                chem << "_nw.w + " << chem << "_n.y + " << chem << "_s.y + " << chem << "_sw.w + " <<
                chem << "_un.x + " << chem << "_u.y + " << chem << "_us.x + " << chem << "_uw.w) * _K2 +\n";
            // then collect the 8 corner-neighbors
            kernel_source << indent << indent << "(" << chem << "_dnw.w + " << chem << "_dn.y + " << chem << "_ds.y + " << chem << "_dsw.w + " <<
                chem << "_unw.w + " << chem << "_un.y + " << chem << "_us.y + " << chem << "_usw.w) * _K3 ,     // x\n";
            // y:
            // first collect the 6 face-neighbors:
            kernel_source << indent << indent << "(" << chem << "_d.y + " << chem << "_n.y + " << chem << ".z + " <<
                chem << "_s.y + " << chem << ".x + " << chem << "_u.y) * _K1 +\n";
            // then collect the 12 edge-neighbors
            kernel_source << indent << indent << "(" << chem << "_dn.y + " << chem << "_d.z + " << chem << "_ds.y + " << chem << "_d.x + " <<
                chem << "_n.x + " << chem << "_n.z + " << chem << "_s.z + " << chem << "_s.x + " <<
                chem << "_un.y + " << chem << "_u.z + " << chem << "_us.y + " << chem << "_u.x) * _K2 +\n";
            // then collect the 8 corner-neighbors
            kernel_source << indent << indent << "(" << chem << "_dn.x + " << chem << "_dn.z + " << chem << "_ds.z + " << chem << "_ds.x + " <<
                chem << "_un.x + " << chem << "_un.z + " << chem << "_us.z + " << chem << "_us.x) * _K3 ,         // y\n";
            // z:
            // first collect the 6 face-neighbors:
            kernel_source << indent << indent << "(" << chem << "_d.z + " << chem << "_n.z + " << chem << ".w + " <<
                chem << "_s.z + " << chem << ".y + " << chem << "_u.z) * _K1 +\n";
            // then collect the 12 edge-neighbors
            kernel_source << indent << indent << "(" << chem << "_dn.z + " << chem << "_d.w + " << chem << "_ds.z + " << chem << "_d.y + " <<
                chem << "_n.y + " << chem << "_n.w + " << chem << "_s.w + " << chem << "_s.y + " <<
                chem << "_un.z + " << chem << "_u.w + " << chem << "_us.z + " << chem << "_u.y) * _K2 +\n";
            // then collect the 8 corner-neighbors
            kernel_source << indent << indent << "(" << chem << "_dn.y + " << chem << "_dn.w + " << chem << "_ds.w + " << chem << "_ds.y + " <<
                chem << "_un.y + " << chem << "_un.w + " << chem << "_us.w + " << chem << "_us.y) * _K3 ,         // z\n";
            // w:
            // first collect the 6 face-neighbors:
            kernel_source << indent << indent << "(" << chem << "_d.w + " << chem << "_n.w + " << chem << "_e.x + " <<
                chem << "_s.w + " << chem << ".z + " << chem << "_u.w) * _K1 +\n";
            // then collect the 12 edge-neighbors
            kernel_source << indent << indent << "(" << chem << "_dn.w + " << chem << "_de.x + " << chem << "_ds.w + " << chem << "_d.z + " <<
                chem << "_n.z + " << chem << "_ne.x + " << chem << "_se.x + " << chem << "_s.z + " <<
                chem << "_un.w + " << chem << "_ue.x + " << chem << "_us.w + " << chem << "_u.z) * _K2 +\n";
            // then collect the 8 corner-neighbors
            kernel_source << indent << indent << "(" << chem << "_dn.z + " << chem << "_dne.x + " << chem << "_dse.x + " << chem << "_ds.z + " <<
                chem << "_un.z + " << chem << "_une.x + " << chem << "_use.x + " << chem << "_us.z) * _K3 )     // w\n";
            // the final entry:
            kernel_source << indent << indent << " + " << chem << " * _K0;\n";
        }
        //            down                              here                              up
        //  (x y z w) (x y z w) (x y z w)   (x y z w) (x y z w) (x y z w)   (x y z w) (x y z w) (x y z w)         dnw   dn  dne      nw   n   ne     unw   un  une
        //  (x y z w) (x y z w) (x y z w)   (x y z w) [x y z w] (x y z w)   (x y z w) (x y z w) (x y z w)  =       dw   d   de        w   .   e       uw   u   ue
        //  (x y z w) (x y z w) (x y z w)   (x y z w) (x y z w) (x y z w)   (x y z w) (x y z w) (x y z w)         dsw   ds  dse      sw   s   se     usw   us  use
    }
    else
    {
        ostringstream oss;
        oss << "FormulaOpenCLImageRD::AssembleKernelSourceFromFormula : unsupported neighborhood options:\n";
        oss << "type=" << this->canonical_neighborhood_type_identifiers.find(this->neighborhood_type)->second << ",\n";
        oss << "dim=" << this->GetArenaDimensionality() << ",\n";
        oss << "range=" << this->neighborhood_range << ",\n";
        oss << "weights=" << this->canonical_neighborhood_weight_identifiers.find(this->neighborhood_weight_type)->second;
        throw runtime_error(oss.str().c_str());
    }
    kernel_source << "\n";
    for(int iC=0;iC<NC;iC++)
        kernel_source << indent << this->data_type_string << "4 delta_" << GetChemicalName(iC) << " = 0.0" << this->data_type_suffix << ";\n";
    kernel_source << "\n";
    // the parameters (assume all float for now)
    for(int i=0;i<(int)this->parameters.size();i++)
        kernel_source << indent << this->data_type_string << "4 " << this->parameters[i].first << " = " << this->parameters[i].second << this->data_type_suffix << ";\n";
    kernel_source << "\n";
    // the formula
    istringstream iss(formula);
    string s;
    while(iss.good())
    {
        getline(iss,s);
        kernel_source << indent << s << "\n";
    }
    // the last part of the kernel
    kernel_source << "\n";
    for(int iC=0;iC<NC;iC++)
        kernel_source << indent << GetChemicalName(iC) << "_out[index_here] = " << GetChemicalName(iC) << " + timestep * delta_" << GetChemicalName(iC) << ";\n";
    kernel_source << "}\n";
    return kernel_source.str();
}

// -------------------------------------------------------------------------

void FormulaOpenCLImageRD::InitializeFromXML(vtkXMLDataElement *rd, bool &warn_to_update)
{
    OpenCLImageRD::InitializeFromXML(rd,warn_to_update);

    vtkSmartPointer<vtkXMLDataElement> rule = rd->FindNestedElementWithName("rule");
    if(!rule) throw runtime_error("rule node not found in file");

    // formula:
    vtkSmartPointer<vtkXMLDataElement> xml_formula = rule->FindNestedElementWithName("formula");
    if(!xml_formula) throw runtime_error("formula node not found in file");

    // number_of_chemicals:
    read_required_attribute(xml_formula,"number_of_chemicals",this->n_chemicals);

    string formula = trim_multiline_string(xml_formula->GetCharacterData());
    //this->TestFormula(formula); // will throw on error
    this->SetFormula(formula); // (won't throw yet)
}

// -------------------------------------------------------------------------

vtkSmartPointer<vtkXMLDataElement> FormulaOpenCLImageRD::GetAsXML(bool generate_initial_pattern_when_loading) const
{
    vtkSmartPointer<vtkXMLDataElement> rd = OpenCLImageRD::GetAsXML(generate_initial_pattern_when_loading);

    vtkSmartPointer<vtkXMLDataElement> rule = rd->FindNestedElementWithName("rule");
    if(!rule) throw runtime_error("rule node not found");

    // formula
    vtkSmartPointer<vtkXMLDataElement> formula = vtkSmartPointer<vtkXMLDataElement>::New();
    formula->SetName("formula");
    formula->SetIntAttribute("number_of_chemicals",this->GetNumberOfChemicals());
    string f = this->GetFormula();
    f = ReplaceAllSubstrings(f, "\n", "\n        "); // indent the lines
    formula->SetCharacterData(f.c_str(), (int)f.length());
    rule->AddNestedElement(formula);

    return rd;
}

// -------------------------------------------------------------------------

void FormulaOpenCLImageRD::SetParameterValue(int iParam,float val)
{
    AbstractRD::SetParameterValue(iParam,val);
    this->need_reload_formula = true;
}

// -------------------------------------------------------------------------

void FormulaOpenCLImageRD::SetParameterName(int iParam,const string& s)
{
    AbstractRD::SetParameterName(iParam,s);
    this->need_reload_formula = true;
}

// -------------------------------------------------------------------------

void FormulaOpenCLImageRD::AddParameter(const std::string& name,float val)
{
    AbstractRD::AddParameter(name,val);
    this->need_reload_formula = true;
}

// -------------------------------------------------------------------------

void FormulaOpenCLImageRD::DeleteParameter(int iParam)
{
    AbstractRD::DeleteParameter(iParam);
    this->need_reload_formula = true;
}

// -------------------------------------------------------------------------

void FormulaOpenCLImageRD::DeleteAllParameters()
{
    AbstractRD::DeleteAllParameters();
    this->need_reload_formula = true;
}

// -------------------------------------------------------------------------

void FormulaOpenCLImageRD::SetWrap(bool w)
{
    AbstractRD::SetWrap(w);
    this->need_reload_formula = true;
}

// -------------------------------------------------------------------------
