// @HEADER
// ************************************************************************
//
//               Rapid Optimization Library (ROL) Package
//                 Copyright (2014) Sandia Corporation
//
// Under terms of Contract DE-AC04-94AL85000, there is a non-exclusive
// license for use of this work by or on behalf of the U.S. Government.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
// 1. Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//
// 2. Redistributions in binary form must reproduce the above copyright
// notice, this list of conditions and the following disclaimer in the
// documentation and/or other materials provided with the distribution.
//
// 3. Neither the name of the Corporation nor the names of the
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY SANDIA CORPORATION "AS IS" AND ANY
// EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
// PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL SANDIA CORPORATION OR THE
// CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
// EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
// PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
// LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
// NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
// SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
// Questions? Contact lead developers:
//              Drew Kouri   (dpkouri@sandia.gov) and
//              Denis Ridzal (dridzal@sandia.gov)
//
// ************************************************************************
// @HEADER

#include "discretization.hpp"
#include "coefficient.hpp"

// ROL Includes
#include "ROL_StdVector.hpp"
#include "ROL_Objective_SimOpt.hpp"
#include "ROL_EqualityConstraint_SimOpt.hpp"

// Teuchos Includes for Linear Algebra
#include "Teuchos_SerialDenseMatrix.hpp"
#include "Teuchos_SerialDenseSolver.hpp"


//#define MANUAL_UPDATE


// Quadratic tracking objective class

using namespace ROL;
template<class Real>
class TrackingObjective : public Objective_SimOpt<Real> {

  template <typename T> using RCP  = Teuchos::RCP<T>;
  template <typename T> using FC   = Intrepid::FieldContainer<T>;            

  typedef Vector<Real>                  V;
  typedef StdVector<Real>               SV;
  typedef std::vector<Real>             vec;

  private:
    RCP<Discretization<Real>> disc_;

    int numCells_;              // Number of cells (elements)
    int numCubPts_;             // Number of cubature points per cells
    int numFields_;             // Number of basis functions per cell
    int spaceDim_;              // Number of spatial dimensions (currently 1)
    int nDoF_;                  // Total number of degrees of freedom

    Real gamma_;

    RCP<FC<Real>> massMatrices_;     

    RCP<V> target_;


    void applyMass(V &Mv, const V &v) {

      using Teuchos::dyn_cast;
      using Teuchos::rcp_const_cast;

      RCP<vec> Mvp = rcp_const_cast<vec>((dyn_cast<SV>(Mv)).getVector()); 
      RCP<const vec> vp = (dyn_cast<SV>(const_cast<V &>(v))).getVector();

      for(int cell=0;cell<numCells_;++cell) {
        for(int rfield=0;rfield<numFields_;++rfield) {
          int i = cell*(numFields_-1) + rfield;
          for(int cfield=0;cfield<numFields_;++cfield) {
            int j = cell*(numFields_-1) + cfield;
            (*Mvp)[i] += (*massMatrices_)(cell,rfield,cfield)*(*vp)[j];  
          }
        }
      }
    }       

  public:    

    TrackingObjective(RCP<Discretization<Real>> disc, const RCP<V> &target, const Real &gamma) : 
      disc_(disc), 
      numCells_(disc_->getNumCells()),
      numCubPts_(disc->getNumCubPts()),
      numFields_(disc->getNumFields()),
      spaceDim_(disc->getSpaceDim()),
      nDoF_(numCells_*(numFields_-1)+1),
      gamma_(gamma), 
      massMatrices_(disc->getMassMatrices()), 
      target_(target)  {}

    Real value(const V &u, const V &z, Real &tol) {

      RCP<V> err = u.clone();
      err->set(u);
      err->axpy(-1.0,*target_);
      RCP<V> Merr = u.clone();
      applyMass(*Merr,*err);
      
      RCP<V> y = z.clone();
      y->set(z);
      RCP<V> My = z.clone();   
      applyMass(*My,*y); 

      Real J = 0.5*(Merr->dot(*err)+gamma_*My->dot(*y));
      return J;       

    } // value()

    void gradient_1( V &g, const V &u, const V &z, Real &tol ) {

      RCP<V> err = u.clone();
      err->set(u);
      err->axpy(-1.0,*target_);
      applyMass(g,*err);
     
      
    } // gradient_1()

    void gradient_2( V &g, const V &u, const V &z, Real &tol ) {

      RCP<V> y = z.clone();
      y->set(z);
      applyMass(g,*y); 
      g.scale(gamma_);

    } // gradient_2()

    void hessVec_11( V &hv, const V &v, const V &u, const V &z, Real &tol) {

      RCP<V> y = v.clone();
      y->set(v);
      applyMass(hv,*y);
         
    } // hessVec_11()  

    void hessVec_12( V &hv, const V &v, const V &u, const V &z, Real &tol) {

      hv.zero();

    } // hessVec_12()  

    void hessVec_21( V &hv, const V &v, const V &u, const V &z, Real &tol) {

      hv.zero();

    } // hessVec_21()  

    void hessVec_22( V &hv, const V &v, const V &u, const V &z, Real &tol) {

      RCP<V> y = v.clone();
      y->set(v);
      applyMass(hv,*y);
      hv.scale(gamma_);
 
    } // hessVec_22()  

};



// BVP Constraint class 

template<class Real> 
class BVPConstraint : public EqualityConstraint_SimOpt<Real> {

  template <typename T> using RCP  = Teuchos::RCP<T>;
  template <typename T> using FC   = Intrepid::FieldContainer<T>;            

  typedef Teuchos::SerialDenseMatrix<int,Real> Matrix;
  typedef Teuchos::SerialDenseSolver<int,Real> Solver;

  typedef Intrepid::FunctionSpaceTools  FST;

  typedef Vector<Real>                  V;
  typedef StdVector<Real>               SV;
  typedef std::vector<Real>             vec;

  private:
    RCP<Discretization<Real>> disc_;

    int numCells_;              // Number of cells (elements)
    int numCubPts_;             // Number of cubature points per cells
    int numFields_;             // Number of basis functions per cell
    int spaceDim_;              // Number of spatial dimensions (currently 1)
    int nDoF_;                  // Total number of degrees of freedom

    RCP<FC<Real>> x_cub_;       // Physical cubature points
    RCP<FC<Real>> tranVals_;    // Transformed values of basis functions
    RCP<FC<Real>> tranGrad_;    // Transformed gradients of basis functions
    RCP<FC<Real>> wtdTranVals_; // Weighted transformed values of basis functions
    RCP<FC<Real>> wtdTranGrad_; // Weighted transformed gradients of basis functions

    RCP<Matrix> Ju_;            // Constraint Jacobian w.r.t. u
    RCP<Matrix> Jz_;            // Constraint Jacobian w.r.t. z

    vec dif_param_;             // Parameters passed to coefficient functions. Currently unused
    vec adv_param_;
    vec rea_param_; 

    enum var { sim, opt };      // Index for simulation and optimization variable

    // Write ROL vector into a one-column Teuchos::SerialDenseMatrix
    void vec2mat(RCP<Matrix> &m, const V &v) {

      using Teuchos::dyn_cast;
      RCP<const vec> vp = (dyn_cast<SV>(const_cast<V &>(v))).getVector();

      for(int i=0;i<nDoF_;++i) {
        (*m)(i,0) = (*vp)[i];  
      }       
    }

    // Write a one-column Teuchos::SerialDenseMatrix into a ROL vector 
    void mat2vec(V &v, const RCP<Matrix> &m) {

       using Teuchos::dyn_cast;
       using Teuchos::rcp_const_cast;

       RCP<vec> vp = rcp_const_cast<vec>((dyn_cast<SV>(v)).getVector()); 
       
       for(int i=0;i<nDoF_;++i) {
         (*vp)[i] = (*m)(i,0);   
       }
    }

    // Gather a ROL vector into a Intrepid Field Container
    template<class ScalarT>
    void gather(FC<ScalarT> &fc, const V& v) {

      using Teuchos::dyn_cast;
      RCP<const vec> vp = (dyn_cast<SV>(const_cast<V &>(v))).getVector();

      for(int cell=0;cell<numCells_;++cell) {
        for(int field=0;field<numFields_;++field) {
          int i = cell*(numFields_-1) + field;
          fc(cell,field) = (*vp)[i]; 
        }
      }
    }

    // Compute the residual given u and z
    template<class ScalarT>
    void evaluate_res(FC<ScalarT> &c_fc, FC<ScalarT> &u_fc, FC<ScalarT> &z_fc) {

       using Teuchos::rcp;

       RCP<Coefficient<Real,ScalarT>> coeff = rcp(new ExampleCoefficient<Real,ScalarT>());

       // Evaluate on the cubature points 
       FC<ScalarT> u_vals_cub(numCells_,numCubPts_);
       FC<ScalarT> z_vals_cub(numCells_,numCubPts_);
       FC<ScalarT> u_grad_cub(numCells_,numCubPts_,spaceDim_);       

       FST::evaluate<ScalarT>(u_vals_cub,u_fc,*tranVals_);
       FST::evaluate<ScalarT>(z_vals_cub,z_fc,*tranVals_);
       FST::evaluate<ScalarT>(u_grad_cub,u_fc,*tranGrad_); 

       // Evaluate terms on the cubature points
       FC<ScalarT> react_cub(numCells_,numCubPts_); 
       FC<ScalarT> advec_cub(numCells_,numCubPts_,spaceDim_); 
       FC<ScalarT> diff_cub(numCells_,numCubPts_); 
 
       coeff->reaction(react_cub, *x_cub_,u_vals_cub,z_vals_cub,rea_param_);
       coeff->advection(advec_cub,*x_cub_,u_vals_cub,z_vals_cub,adv_param_);
       coeff->diffusion(diff_cub, *x_cub_,u_vals_cub,z_vals_cub,dif_param_);

       FC<ScalarT> advec_term(numCells_,numCubPts_);
       FC<ScalarT> diff_term(numCells_,numCubPts_,spaceDim_);
       
       FST::scalarMultiplyDataData<ScalarT>(diff_term,diff_cub,u_grad_cub); 
       FST::dotMultiplyDataData<ScalarT>(advec_term,advec_cub,u_grad_cub);
 
       // Add terms to residual
       c_fc.initialize();
       FST::integrate<ScalarT>(c_fc,diff_term, *wtdTranGrad_,Intrepid::COMP_CPP,false);  
       FST::integrate<ScalarT>(c_fc,advec_term,*wtdTranVals_,Intrepid::COMP_CPP,true);
       FST::integrate<ScalarT>(c_fc,react_cub, *wtdTranVals_,Intrepid::COMP_CPP,true);

    }

    void applyJac(V &jv, const V &v, var comp, bool transpose = false) {

       using Teuchos::dyn_cast;
       using Teuchos::rcp_const_cast;

       RCP<Matrix> J = (comp==sim) ? Ju_ : Jz_;

       // Downcast and extract RCPs to std::vectors
       RCP<vec> jvp = rcp_const_cast<vec>((dyn_cast<SV>(jv)).getVector()); 

       std::fill(jvp->begin(),jvp->end(),0.0);
       RCP<const vec> vp = (dyn_cast<SV>(const_cast<V &>(v))).getVector();

       for(int row=0;row<nDoF_;++row) {
         for(int col=0;col<nDoF_;++col) {
           if(transpose) {
             (*jvp)[row] += (*J)(col,row)*(*vp)[col];
           } else {
             (*jvp)[row] += (*J)(row,col)*(*vp)[col];
           }
         } 
       }
    }

    void applyAdjointHessian(V &ahwv, const V &w, const V &v, 
                             const V  &u, const V &z, var one, var two )  {

      using Teuchos::rcp_const_cast;     
      using Teuchos::dyn_cast;

      typedef Sacado::Fad::SFad<Real,1> SFad;
      typedef Sacado::Fad::DFad<SFad>   DSFad;

      RCP<vec> ahwvp = rcp_const_cast<vec>((dyn_cast<SV>(ahwv)).getVector());

      std::fill(ahwvp->begin(),ahwvp->end(),0.0);

      RCP<const vec> vp = (dyn_cast<SV>(const_cast<V &>(v))).getVector();
      RCP<const vec> wp = (dyn_cast<SV>(const_cast<V &>(w))).getVector();
      RCP<const vec> up = (dyn_cast<SV>(const_cast<V &>(u))).getVector();
      RCP<const vec> zp = (dyn_cast<SV>(const_cast<V &>(z))).getVector();

      FC<DSFad> u_fc(numCells_,numFields_);
      FC<DSFad> z_fc(numCells_,numFields_);

      if(one == sim && two == sim) { // H11 block
        for(int cell=0;cell<numCells_;++cell) {
          for(int field=0;field<numFields_;++field) {
            int i = cell*(numFields_-1) + field; 
            SFad temp(1,(*up)[i]);
            temp.fastAccessDx(0) = (*vp)[i]; 
            u_fc(cell,field) = DSFad(numFields_,field,temp);
            z_fc(cell,field) = (*zp)[i];
          }
        } 
      }
      else if(one == sim && two == opt) { // H12 block
        for(int cell=0;cell<numCells_;++cell) {
          for(int field=0;field<numFields_;++field) {
            int i = cell*(numFields_-1) + field; 
            SFad temp(1,(*up)[i]);
            temp.fastAccessDx(0) = (*vp)[i]; 
            u_fc(cell,field) = temp;
            z_fc(cell,field) = DSFad(numFields_,field,(*zp)[i]);
          }
        } 
      }
      else if(one == opt && two == sim) { // H21 block
        for(int cell=0;cell<numCells_;++cell) {
          for(int field=0;field<numFields_;++field) {
            int i = cell*(numFields_-1) + field; 
            SFad temp(1,(*zp)[i]);
            temp.fastAccessDx(0) = (*vp)[i]; 
            z_fc(cell,field) = temp;
            u_fc(cell,field) = DSFad(numFields_,field,(*up)[i]);
          }
        } 
      }
      else { // H22 block
        for(int cell=0;cell<numCells_;++cell) {
          for(int field=0;field<numFields_;++field) {
            int i = cell*(numFields_-1) + field; 
            SFad temp(1,(*zp)[i]);
            temp.fastAccessDx(0) = (*vp)[i]; 
            z_fc(cell,field) = DSFad(numFields_,field,temp);
            u_fc(cell,field) = (*up)[i];
          }
        } 
      }

      FC<DSFad> c_fc(numCells_,numFields_);         

      evaluate_res(c_fc,u_fc,z_fc);

     // Compute the cellwise dot product of the constraint value and the Lagrange multiply
     for(int cell=0;cell<numCells_;++cell) {
       DSFad wdotc(SFad(0.0));
       for(int field=0;field<numFields_;++field) {
         int i = cell*(numFields_-1) + field; 
         wdotc += c_fc(cell,field)*(*wp)[i];
       }
       for(int field=0;field<numFields_;++field) {
         int i = cell*(numFields_-1) + field; 
         (*ahwvp)[i] += wdotc.dx(field).dx(0);
       }
      } 
    }


  public:

    // Constructor
    BVPConstraint(const RCP<Discretization<Real>> disc) : 
      disc_(disc),
      numCells_(disc->getNumCells()),
      numCubPts_(disc->getNumCubPts()),
      numFields_(disc->getNumFields()),
      spaceDim_(disc->getSpaceDim()),
      nDoF_(numCells_*(numFields_-1)+1),
      x_cub_(disc->getPhysCubPts()),
      tranVals_(disc_->getTransformedVals()),
      tranGrad_(disc_->getTransformedGrad()),
      wtdTranVals_(disc_->getWeightedTransformedVals()),
      wtdTranGrad_(disc_->getWeightedTransformedGrad()),
      Ju_(Teuchos::rcp(new Matrix(nDoF_,nDoF_,true))),
      Jz_(Teuchos::rcp(new Matrix(nDoF_,nDoF_,true))) {
     
    }


    void update( const V& u, const V &z, bool flag, int iter = -1 ) {

        using Sacado::Fad::DFad;        

        using Teuchos::dyn_cast;
        typedef DFad<Real> FadType;

        RCP<const vec> up = (dyn_cast<SV>(const_cast<V &>(u))).getVector();
        RCP<const vec> zp = (dyn_cast<SV>(const_cast<V &>(z))).getVector();

        FC<FadType> u_fc1(numCells_,numFields_); 
        FC<FadType> z_fc1(numCells_,numFields_);
        FC<FadType> u_fc2(numCells_,numFields_); 
        FC<FadType> z_fc2(numCells_,numFields_);

        gather(z_fc1,z);
        gather(u_fc2,u);

        // Fill in field containers from vectors (gather)
        for(int cell=0;cell<numCells_;++cell) {
          for(int field=0;field<numFields_;++field) {
            int i = cell*(numFields_-1) + field;
            u_fc1(cell,field) = FadType(numFields_,field,(*up)[i]);
            z_fc2(cell,field) = FadType(numFields_,field,(*zp)[i]);
          } 
        }       

        FC<FadType> c_fc1(numCells_,numFields_); 
        FC<FadType> c_fc2(numCells_,numFields_); 

        evaluate_res(c_fc1,u_fc1,z_fc1); 
        evaluate_res(c_fc2,u_fc2,z_fc2); 

        Ju_->putScalar(0.0); // Zero out the matrix
        Jz_->putScalar(0.0); // Zero out the matrix

        for(int cell=0;cell<numCells_;++cell) {
          for(int rfield=0;rfield<numFields_;++rfield) {
            int row = cell*(numFields_-1) + rfield;
            for(int cfield=0;cfield<numFields_;++cfield) {
              int col = cell*(numFields_-1) + cfield;
              (*Ju_)(row,col) += c_fc1(cell,rfield).dx(cfield);
              (*Jz_)(row,col) += c_fc2(cell,rfield).dx(cfield);
            }
          } 
        }       

    }


    void value(V &c, const V &u, const V &z, Real &tol=0) {

#ifdef MANUAL_UPDATE      
      update(u,z,true);
#endif

      using Teuchos::rcp_const_cast;
      using Teuchos::dyn_cast;
      // Downcast and extract RCPs to std::vectors
      RCP<vec> cp = rcp_const_cast<vec>((dyn_cast<SV>(c)).getVector()); 

      std::fill(cp->begin(),cp->end(),0.0); 

      FC<Real> u_fc(numCells_,numFields_); 
      FC<Real> z_fc(numCells_,numFields_);

      gather(u_fc,u);
      gather(z_fc,z);

      FC<Real> c_fc(numCells_,numFields_);

      evaluate_res(c_fc,u_fc,z_fc);
 
      // Scatter residual back into ROL vector
      for(int cell=0;cell<numCells_;++cell) {
       for(int field=0;field<numFields_;++field) {
          int i = cell*(numFields_-1) + field;
          (*cp)[i] += c_fc(cell,field);
        } 
      }       
   } // value()


    void applyJacobian_1(V &jv, const V &v, const V &u, const V &z, Real &tol) {
#ifdef MANUAL_UPDATE
      update(u,z,true);
#endif
      applyJac(jv,v,sim,false);

    } // applyJacobian_1()

   
    void applyJacobian_2(V &jv, const V &v, const V &u, const V &z, Real &tol) {
#ifdef MANUAL_UPDATE
      update(u,z,true);
#endif
      applyJac(jv,v,opt,false);

    }  // applyJacobian_2()

      
    void applyAdjointJacobian_1(V &jv, const V &v, const V &u, const V &z, Real &tol) {
#ifdef MANUAL_UPDATE
      update(u,z,true);
#endif
      applyJac(jv,v,sim,true);

    } // applyAdjointJacobian_1()

   
    void applyAdjointJacobian_2(V &jv, const V &v, const V &u, const V &z, Real &tol) {
#ifdef MANUAL_UPDATE
      update(u,z,true);
#endif
      applyJac(jv,v,opt,true);

    }  // applyAdjointJacobian_2()


    void applyInverseJacobian_1(V &ijv, const V &v, const V &u, const V &z, Real &tol) {
#ifdef MANUAL_UPDATE
      update(u,z,true);
#endif
      Teuchos::SerialDenseSolver<int,Real> solver;

      // Must use a temporary matrix because LAPACK overwrites the original with factors.
      RCP<Matrix> Ju_temp = Teuchos::rcp(new Matrix(*Ju_));

      solver.setMatrix(Ju_temp);
      solver.factorWithEquilibration(true);
      solver.factor();

      RCP<Matrix> rhs = Teuchos::rcp(new Matrix(nDoF_,1));
      RCP<Matrix> sol = Teuchos::rcp(new Matrix(nDoF_,1));
       
      vec2mat(rhs,v);             // Write the ROL vector into the rhs 
      solver.setVectors(sol,rhs); 
      solver.solveWithTranspose(true);
      solver.solve();             // Solve the system
      solver.solveWithTranspose(false);
      mat2vec(ijv,sol);           // Write the solution into the ROL vector
      

    } // applyInverseJacobian_1()


    void applyInverseAdjointJacobian_1(V &iajv, const V &v, const V &u, const V &z, Real &tol) {
#ifdef MANUAL_UPDATE
      update(u,z,true);
#endif
      Teuchos::SerialDenseSolver<int,Real> solver;

      // Must use a temporary matrix because LAPACK overwrites the original with factors.
      RCP<Matrix> Ju_temp = Teuchos::rcp(new Matrix(*Ju_));

      solver.setMatrix(Ju_temp);
      solver.factorWithEquilibration(true);
      solver.factor();

      RCP<Matrix> rhs = Teuchos::rcp(new Matrix(nDoF_,1));
      RCP<Matrix> sol = Teuchos::rcp(new Matrix(nDoF_,1));
       
      vec2mat(rhs,v);                     // Write the ROL vector into the rhs 
      solver.setVectors(sol,rhs); 
      solver.solveWithTranspose(true);
      solver.solve();                     // Solve the system
      solver.solveWithTranspose(false);
      mat2vec(iajv,sol);                  // Write the solution into the ROL vector


    } // applyInverseAdjointJacobian_1()

    

    void applyAdjointHessian_11(V &ahwv, const V &w, const V &v, 
                                const V  &u, const V &z, Real &tol ) {
#ifdef MANUAL_UPDATE
      update(u,z,true);
#endif
      applyAdjointHessian(ahwv, w, v, u, z, sim, sim);

    } // applyAdjointHessian_11()


    void applyAdjointHessian_12(V &ahwv, const V &w, const V &v, 
                                const V  &u, const V &z, Real &tol ) {
#ifdef MANUAL_UPDATE
      update(u,z,true);
#endif
      applyAdjointHessian(ahwv, w, v, u, z, sim, opt);
     
    } // applyAdjointHessian_11()

    void applyAdjointHessian_21(V &ahwv, const V &w, const V &v, 
                                const V  &u, const V &z, Real &tol ) {
#ifdef MANUAL_UPDATE
      update(u,z,true);
#endif
      applyAdjointHessian(ahwv, w, v, u, z, opt, sim);
    } // applyAdjointHessian_22()

    void applyAdjointHessian_22(V &ahwv, const V &w, const V &v, 
                                const V  &u, const V &z, Real &tol ) {
#ifdef MANUAL_UPDATE
      update(u,z,true);
#endif
      applyAdjointHessian(ahwv, w, v, u, z, opt, opt);
    } // applyAdjointHessian_22()

};


