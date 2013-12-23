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

#ifndef ROL_STEP_H
#define ROL_STEP_H

#include "ROL_Vector.hpp"
#include "Teuchos_ParameterList.hpp"

/** \class ROL::Step
    \brief Provides the interface to compute optimization steps.
*/


namespace ROL {

template<class Real>
struct AlgorithmState {
  int  iter;
  int  nfval;
  int  ngrad;
  Real value;
  Real gnorm;
  Real snorm;
  Teuchos::RCP<Vector<Real> > iterateVec;
};

template<class Real>
struct StepState {
  Teuchos::RCP<Vector<Real> > gradientVec;
  Teuchos::RCP<Vector<Real> > descentVec;
};


template <class Real>
class Step {
private:

public:
  Teuchos::RCP<StepState<Real> > state_;

  virtual ~Step() {}

  Step(void) { 
    state_ = Teuchos::rcp( new StepState<Real> );
  }

  Teuchos::RCP<StepState<Real> >& get_state() { return this->state_; }

  /** \brief Initialize step.
  */
  virtual void initialize( const Vector<Real> &x, Objective<Real> &obj, AlgorithmState<Real> &algo_state ) {
    Real tol = std::sqrt(ROL_EPSILON);
    state_->descentVec  = x.clone();
    state_->gradientVec = x.clone();
    obj.update(x,true,algo_state.iter);
    obj.gradient(*(state_->gradientVec),x,tol);
    algo_state.ngrad = 1;
    algo_state.gnorm = (state_->gradientVec)->norm();
    algo_state.snorm = 1.e10;
    algo_state.value = obj.value(x,tol);
    algo_state.nfval = 1;
  }

  /** \brief Compute step.
  */
  virtual void compute( Vector<Real> &s, const Vector<Real> &x, Objective<Real> &obj, 
                        AlgorithmState<Real> &algo_state ) = 0;

  /** \brief Update step, if successful.
  */
  virtual void update( Vector<Real> &x, const Vector<Real> &s, Objective<Real> &obj, 
                       AlgorithmState<Real> &algo_state ) = 0;

  /** \brief Print iterate header.
  */
  virtual std::string printHeader( void ) const = 0;

  /** \brief Print step name.
  */
  virtual std::string printName( void ) const = 0;

  /** \brief Print iterate status.
  */
  virtual std::string print( AlgorithmState<Real> &algo_state, bool printHeader = false ) const = 0;

  // struct StepState (scalars, vectors) map?

  // getState

  // setState

}; // class Step

} // namespace ROL

#endif
