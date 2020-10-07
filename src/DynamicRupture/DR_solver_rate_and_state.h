//
// Created by adrian on 03.09.20.
//

#ifndef SEISSOL_DR_SOLVER_RATE_AND_STATE_H
#define SEISSOL_DR_SOLVER_RATE_AND_STATE_H

#include "DR_solver_base.h"

#include "DR_math.h"
#include <yaml-cpp/yaml.h>



namespace seissol {
  namespace dr {
    namespace fr_law {
      class Solver_FL_3; //rate and state aging law
      class Solver_FL_4; //rate and state slip law
      class RateAndStateNucFL103;  //rate and state nuc103
      class RateAndStateThermalFL103;
      class SolverRateAndStateVwFL7;
    }
  }
}

class seissol::dr::fr_law::Solver_FL_3 : public seissol::dr::fr_law::BaseFrictionSolver {
protected:
  virtual real calcStateVariableHook(real SV0, real tmp, real time_inc, real RS_sl0) {
    return SV0*exp(-tmp*time_inc/RS_sl0)+RS_sl0/tmp*(1.0-exp(-tmp*time_inc/RS_sl0));
  }

public:
  virtual void evaluate(seissol::initializers::Layer&  layerData,
                        seissol::initializers::DynamicRupture *dynRup,
                        real (*QInterpolatedPlus)[CONVERGENCE_ORDER][tensor::QInterpolated::size()],
                        real (*QInterpolatedMinus)[CONVERGENCE_ORDER][tensor::QInterpolated::size()],
                        real fullUpdateTime,
                        real timeWeights[CONVERGENCE_ORDER],
                        real DeltaT[CONVERGENCE_ORDER]) override {


    seissol::initializers::DR_FL_3 *ConcreteLts = dynamic_cast<seissol::initializers::DR_FL_3 *>(dynRup);

    seissol::model::IsotropicWaveSpeeds *waveSpeedsPlus                           = layerData.var(ConcreteLts->waveSpeedsPlus);
    seissol::model::IsotropicWaveSpeeds *waveSpeedsMinus                          = layerData.var(ConcreteLts->waveSpeedsMinus);
    real                    (*initialStressInFaultCS)[numOfPointsPadded][6]       = layerData.var(ConcreteLts->initialStressInFaultCS);
    real                    (*cohesion)[numOfPointsPadded]                        = layerData.var(ConcreteLts->cohesion);

    real*                   RS_a                                                  = layerData.var(ConcreteLts->RS_a);
    real*                   RS_sl0                                                = layerData.var(ConcreteLts->RS_sl0);
    real*                   RS_sr0                                                = layerData.var(ConcreteLts->RS_sr0);

    real                    (*mu)[numOfPointsPadded]                              = layerData.var(ConcreteLts->mu);
    real                    (*slip)[numOfPointsPadded]                            = layerData.var(ConcreteLts->slip);
    real                    (*slip1)[numOfPointsPadded]                           = layerData.var(ConcreteLts->slip1);
    real                    (*slip2)[numOfPointsPadded]                           = layerData.var(ConcreteLts->slip2);
    real                    (*slipRate1)[numOfPointsPadded]                       = layerData.var(ConcreteLts->slipRate1);
    real                    (*slipRate2)[numOfPointsPadded]                       = layerData.var(ConcreteLts->slipRate2);
    real                    (*StateVar)[numOfPointsPadded]                        = layerData.var(ConcreteLts->StateVar);

    real                    (*tracXY)[numOfPointsPadded]                          = layerData.var(ConcreteLts->tracXY);
    real                    (*tracXZ)[numOfPointsPadded]                          = layerData.var(ConcreteLts->tracXZ);


    //loop parameter are fixed, not variable??
    unsigned int nSRupdates, nSVupdates;
    nSRupdates = 5;
    nSVupdates = 2;


#ifdef _OPENMP
#pragma omp parallel for schedule(static) //private(QInterpolatedPlus,QInterpolatedMinus)
#endif
    for (unsigned face = 0; face < layerData.getNumberOfCells(); ++face) {

      FaultStresses faultStresses{};

      precomputeStressFromQInterpolated(faultStresses, QInterpolatedPlus[face], QInterpolatedMinus[face], face);

      real LocSlip, LocSlip1, LocSlip2, LocSR1, LocSR2, LocSV, LocCohesion, P_0, LocP, time_inc, P, TotalShearStressYZ, SV0, tmp,tmp2, SlipRateGuess, NR, dNR, LocMu;
      real LocTracXY, LocTracXZ;
      real LocSlipRate[numberOfPoints];

      for(int iBndGP = 0; iBndGP < numOfPointsPadded; iBndGP++) {

        LocSlip = slip[face][iBndGP]; //DISC%DynRup%Slip(iBndGP,iFace)              //!< Slip path at given fault node
        LocSlip1 = slip1[face][iBndGP]; //DISC%DynRup%Slip1(iBndGP,iFace)            //!< Slip at given fault node along loc dir 1
        LocSlip2 = slip2[face][iBndGP]; //DISC%DynRup%Slip2(iBndGP,iFace)            // !< Slip at given fault node along loc dir 2
        LocSR1 = slipRate1[face][iBndGP]; //DISC%DynRup%SlipRate1(iBndGP,iFace)         // !< Slip Rate at given fault node
        LocSR2 = slipRate2[face][iBndGP]; //DISC%DynRup%SlipRate2(iBndGP,iFace)         // !< Slip Rate at given fault node
        LocSV = StateVar[face][iBndGP];     //DISC%DynRup%StateVar(iBndGP,iFace)
        LocCohesion = cohesion[face][iBndGP]; //DISC%DynRup%cohesion(iBndGP,iFace)          // !< cohesion at given fault node  (should be negative since negative normal stress is compression)
        P_0 = initialStressInFaultCS[face][iBndGP][0]; //EQN%InitialStressInFaultCS[iBndGP][1][iFace];

        for (int iTimeGP = 0; iTimeGP < CONVERGENCE_ORDER; iTimeGP++) {
          LocP = faultStresses.NorStressGP[iTimeGP][iBndGP];
          time_inc = DeltaT[iTimeGP];

          //SignSR1   = SIGN(1.0,LocSR1)                    ! Gets the sign of the slip rate
          //SignSR2   = SIGN(1.0,LocSR2)                    ! Gets the sign of the slip rate

          // load traction and normal stress
          P = LocP + P_0;

          TotalShearStressYZ = std::sqrt(
              seissol::dr::aux::power(initialStressInFaultCS[face][iBndGP][3] + faultStresses.XYStressGP[iTimeGP][iBndGP], 2) +
              seissol::dr::aux::power(initialStressInFaultCS[face][iBndGP][5] + faultStresses.XZStressGP[iTimeGP][iBndGP], 2));

          // We use the regularized rate-and-state friction, after Rice & Ben-Zion (1996)
          // ( Numerical note: ASINH(X)=LOG(X+SQRT(X^2+1)) )

          SV0 = LocSV;    // Careful, the SV must always be corrected using SV0 and not LocSV!

          // The following process is adapted from that described by Kaneko et al. (2008)
          LocSlipRate[iBndGP]      = std::sqrt(seissol::dr::aux::power(LocSR1,2) + seissol::dr::aux::power(LocSR2,2));
          tmp        = fabs( LocSlipRate[iBndGP]);

          for(int j = 0; j < nSVupdates; j++){ //!This loop corrects SV values
            LocSlipRate[iBndGP]=fabs( LocSlipRate[iBndGP]);

            // FL= 3 aging law and FL=4 slip law
            LocSV = calcStateVariableHook( SV0,  tmp,  time_inc,  RS_sl0[face]);

            // Newton-Raphson algorithm to determine the value of the slip rate.
            // We wish to find SR that fulfills g(SR)=f(SR), by building up the function NR=f-g , which has
            //  a derivative dNR = d(NR)/d(SR). We can then find SR by iterating SR_{i+1}=SR_i-( NR_i / dNR_i ).
            // In our case we equalize the values of the traction for two equations:
            //             g = SR*mu/2/cs + T^G             (eq. 18 of de la Puente et al. (2009))
            //             f = (mu*P_0-|S_0|)*S_0/|S_0|     (Coulomb's model of friction)
            //               where mu=a*asinh(SR/2/SR0*exp((F0+b*log(SR0*SV/L))/a (eq. 2a of Lapusta and Rice (2003))

            SlipRateGuess = LocSlipRate[iBndGP];   // SRtest: We use as first guess the SR value of the previous time step

            for(int i = 0; i < nSRupdates; i++){   //!This loop corrects SR values
              tmp          = 0.5/RS_sr0[face]* exp( (m_Params->rs_f0+m_Params->rs_b*log(RS_sr0[face]*LocSV/RS_sl0[face]) ) /RS_a[face]);
              tmp2         = tmp * SlipRateGuess;
              NR           = -(1.0/waveSpeedsPlus->sWaveVelocity/waveSpeedsPlus->density+1.0/waveSpeedsMinus->sWaveVelocity/waveSpeedsMinus->density) *
                             (fabs(P)*RS_a[face]*log(tmp2+sqrt(seissol::dr::aux::power(tmp2,2)+1.0))-TotalShearStressYZ)-SlipRateGuess;    //!TODO: author before me: not sure if ShTest=TotalShearStressYZ should be + or -...
              dNR          = -(1.0/waveSpeedsPlus->sWaveVelocity/waveSpeedsPlus->density+1.0/waveSpeedsMinus->sWaveVelocity/waveSpeedsMinus->density) *
                             (fabs(P)*RS_a[face]/sqrt(1+seissol::dr::aux::power(tmp2,2))*tmp)-1.0;
              SlipRateGuess = fabs(SlipRateGuess-NR/dNR);             // no ABS needed around NR/dNR at least for aging law
            }   // End
            tmp=0.5*( LocSlipRate[iBndGP]+fabs(SlipRateGuess));  //! For the next SV update, use the mean slip rate between the initial guess and the one found (Kaneko 2008, step 6)
            LocSlipRate[iBndGP]=fabs(SlipRateGuess);
          }   // End SV-Loop

          // FL= 3 aging law and FL=4 slip law
          LocSV= calcStateVariableHook( SV0,  tmp,  time_inc,  RS_sl0[face]);

          //TODO: reused calc from above -> simplify
          tmp  = 0.5 * ( LocSlipRate[iBndGP])/RS_sr0[face] * exp((m_Params->rs_f0 + m_Params->rs_b*log(RS_sr0[face]*LocSV/RS_sl0[face])) / RS_a[face]);

          LocMu    = RS_a[face] * log(tmp + sqrt(seissol::dr::aux::power(tmp,2) + 1.0));

          // 2D:
          // LocTrac  = -(ABS(S_0)-LocMu*(LocP+P_0))*(S_0/ABS(S_0))
          // LocTrac  = ABS(LocTrac)*(-SignSR)  !!! line commented as it leads NOT to correct results
          // update stress change
          LocTracXY = -((initialStressInFaultCS[face][iBndGP][3] + faultStresses.XYStressGP[iBndGP][iTimeGP])/TotalShearStressYZ)*(LocMu*P+fabs(LocCohesion));
          LocTracXZ = -((initialStressInFaultCS[face][iBndGP][5] + faultStresses.XZStressGP[iBndGP][iTimeGP])/TotalShearStressYZ)*(LocMu*P+fabs(LocCohesion));
          LocTracXY = LocTracXY - initialStressInFaultCS[face][iBndGP][3];
          LocTracXZ = LocTracXZ - initialStressInFaultCS[face][iBndGP][5];

          // Compute slip
          LocSlip   = LocSlip  + ( LocSlipRate[iBndGP])*time_inc; // ABS of LocSR removed as it would be the accumulated slip that is usually not needed in the solver, see linear slip weakening

          //Update slip rate (notice that LocSR(T=0)=-2c_s/mu*s_xy^{Godunov} is the slip rate caused by a free surface!)
          LocSR1     = -(1.0/(waveSpeedsPlus->sWaveVelocity*waveSpeedsPlus->density)+1.0/(waveSpeedsMinus->sWaveVelocity*waveSpeedsMinus->density))*(LocTracXY-faultStresses.XYStressGP[iTimeGP][iBndGP]);
          LocSR2     = -(1.0/(waveSpeedsPlus->sWaveVelocity*waveSpeedsPlus->density)+1.0/(waveSpeedsMinus->sWaveVelocity*waveSpeedsMinus->density))*(LocTracXZ-faultStresses.XZStressGP[iTimeGP][iBndGP]);

          LocSlip1   = LocSlip1  + (LocSR1)*time_inc;
          LocSlip2   = LocSlip2  + (LocSR2)*time_inc;

          //LocSR1     = SignSR1*ABS(LocSR1)
          //LocSR2     = SignSR2*ABS(LocSR2)

          //Save traction for flux computation
          faultStresses.TractionGP_XY[iTimeGP][iBndGP] = LocTracXY;
          faultStresses.TractionGP_XZ[iTimeGP][iBndGP] = LocTracXZ;
        }//End of iTimeGP- loop

        mu[face][iBndGP]        = LocMu;
        slipRate1[face][iBndGP] = LocSR1;
        slipRate2[face][iBndGP] = LocSR2;
        slip[face][iBndGP]      = LocSlip;
        slip1[face][iBndGP]     = LocSlip1;
        slip2[face][iBndGP]     = LocSlip2;
        StateVar[face][iBndGP]  = LocSV;
        tracXY[face][iBndGP]    = LocTracXY;
        tracXZ[face][iBndGP]    = LocTracXZ;

      }//End of iBndGP-loop

      // output rupture front
      // outside of iTimeGP loop in order to safe an 'if' in a loop
      // this way, no subtimestep resolution possible
      outputRuptureFront(LocSlipRate, fullUpdateTime, face);

      calcPeakSlipRate(LocSlipRate, face);


      postcomputeImposedStateFromNewStress(QInterpolatedPlus[face], QInterpolatedMinus[face],
                                           faultStresses, timeWeights, face);
    } //end face-loop
  } //end evaluate function
};

class seissol::dr::fr_law::Solver_FL_4 : public seissol::dr::fr_law::Solver_FL_3 {
public:

  virtual real calcStateVariableHook(real SV0, real tmp, real time_inc, real RS_sl0) override {
    return RS_sl0/tmp*seissol::dr::aux::power(tmp*SV0/RS_sl0, exp(-tmp*time_inc/RS_sl0));
  }

};


class seissol::dr::fr_law::RateAndStateNucFL103 : public seissol::dr::fr_law::BaseFrictionSolver {
protected:
  //Attributes
  real  (*nucleationStressInFaultCS)[numOfPointsPadded][6];
  real dt = 0;

  real  (*RS_a_array)[numOfPointsPadded];
  real  (*RS_srW_array)[numOfPointsPadded];
  real  (*RS_sl0_array)[numOfPointsPadded];

  bool  (*DS)[numOfPointsPadded];
  real  (*stateVar)[numOfPointsPadded];
  real  (*dynStress_time)[numOfPointsPadded];


  /*
   * copies all parameters from the DynamicRupture LTS to the local attributes
   */
  void copyLtsTreeToLocal(seissol::initializers::Layer&  layerData,
                          seissol::initializers::DynamicRupture *dynRup) override {
    //first copy all Variables from the Base Lts dynRup tree
    BaseFrictionSolver::copyLtsTreeToLocal(layerData, dynRup);
    //TODO: change later to const_cast
    seissol::initializers::DR_FL_103 *ConcreteLts = dynamic_cast<seissol::initializers::DR_FL_103 *>(dynRup);
    nucleationStressInFaultCS =  layerData.var(ConcreteLts->nucleationStressInFaultCS); ;

    RS_sl0_array    = layerData.var(ConcreteLts->RS_sl0_array);
    RS_a_array      = layerData.var(ConcreteLts->RS_a_array);
    RS_srW_array    = layerData.var(ConcreteLts->RS_srW_array);
    DS              = layerData.var(ConcreteLts->DS);
    averaged_Slip   = layerData.var(ConcreteLts->averaged_Slip);
    stateVar        = layerData.var(ConcreteLts->stateVar);
    dynStress_time  = layerData.var(ConcreteLts->dynStress_time);
  }

  void updateStateVariable(int iBndGP, unsigned int face, real SV0, real time_inc, real &SR_tmp, real &LocSV){
    double flv, fss, SVss;
    double RS_fw = m_Params->mu_w;
    double RS_srW = RS_srW_array[face][iBndGP];
    double RS_a = RS_a_array[face][iBndGP];
    double RS_sl0 = RS_sl0_array[face][iBndGP];
    double exp1;

    // low-velocity steady state friction coefficient
    flv = m_Params->rs_f0 - (m_Params->rs_b-RS_a)* log(SR_tmp/m_Params->rs_sr0);
    // steady state friction coefficient
    fss = RS_fw + (flv - RS_fw)/pow(1.0+seissol::dr::aux::power(SR_tmp/RS_srW,8.0) ,1.0/8.0);
    // steady-state state variable
    // For compiling reasons we write SINH(X)=(EXP(X)-EXP(-X))/2
    SVss = RS_a * log(2.0*m_Params->rs_sr0/SR_tmp * (exp(fss/RS_a)-exp(-fss/RS_a))/2.0);

    // exact integration of dSV/dt DGL, assuming constant V over integration step

    exp1 = exp(-SR_tmp*(time_inc/RS_sl0) );
    LocSV = SVss*(1.0-exp1)+exp1*SV0;

    assert( !std::isnan(LocSV) && "NaN detected");
  }

  /*
   * If the function did not converge it returns false
   */
  bool IterativelyInvertSR (unsigned int ltsFace, int nSRupdates, real LocSR[numberOfPoints],
                            std::array<real, numOfPointsPadded> &LocSV, std::array<real, numOfPointsPadded> &n_stress,
                            std::array<real, numOfPointsPadded> &sh_stress, std::array<real, numOfPointsPadded> &SRtest ){

    double tmp[numberOfPoints], tmp2[numberOfPoints], tmp3[numberOfPoints], mu_f[numberOfPoints], dmu_f[numberOfPoints], NR[numberOfPoints], dNR[numberOfPoints];
    double aTolF = 1e-8;
    double AlmostZero = 1e-45;
    bool has_converged = false;

    //!solve for Vnew = SR , applying the Newton-Raphson algorithm
    //!SR fulfills g(SR)=f(SR)
    //!-> find root of NR=f-g using a Newton-Raphson algorithm with dNR = d(NR)/d(SR)
    //!SR_{i+1}=SR_i-( NR_i / dNR_i )
    //!
    //!        equalize:
    //!         g = SR*MU/2/cs + T^G             (eq. 18 of de la Puente et al. (2009))
    //!         f = (mu*P_0-|S_0|)*S_0/|S_0|     (Coulomb's model of friction)
    //!  where mu = friction coefficient, dependening on the RSF law used

    //TODO: padded?
    for(int iBndGP = 0; iBndGP < numberOfPoints; iBndGP++){
      //! first guess = SR value of the previous step
      SRtest[iBndGP] = LocSR[iBndGP];
      tmp[iBndGP]   =  0.5 / m_Params->rs_sr0 *exp(LocSV[iBndGP]/RS_a_array[ltsFace][iBndGP]);
    }

    for(int i = 0; i < nSRupdates; i++){
      //TODO: padded?
      for(int iBndGP = 0; iBndGP < numberOfPoints; iBndGP++){

        //!f = ( tmp2 * ABS(LocP+P_0)- ABS(S_0))*(S_0)/ABS(S_0)
        //!g = SRtest * 1.0/(1.0/w_speed(2)/rho+1.0/w_speed_neig(2)/rho_neig) + ABS(ShTest)
        //!for compiling reasons ASINH(X)=LOG(X+SQRT(X^2+1))

        //!calculate friction coefficient
        tmp2[iBndGP]  = tmp[iBndGP]*SRtest[iBndGP];
        mu_f[iBndGP]  = RS_a_array[ltsFace][iBndGP] * log(tmp2[iBndGP] + sqrt(seissol::dr::aux::power(tmp2[iBndGP], 2) + 1.0));
        dmu_f[iBndGP] = RS_a_array[ltsFace][iBndGP] / sqrt(1.0 + seissol::dr::aux::power(tmp2[iBndGP], 2)) * tmp[iBndGP];
        NR[iBndGP]    = -impAndEta[ltsFace].inv_eta_s * (fabs(n_stress[iBndGP]) * mu_f[iBndGP] - sh_stress[iBndGP]) - SRtest[iBndGP];
      }

      has_converged = true;

      //TODO: padded?
      //max element of NR must be smaller then aTolF
      for(int iBndGP = 0; iBndGP < numberOfPoints; iBndGP++){
        if (fabs(NR[iBndGP]) >= aTolF ){
          has_converged = false;
          break;
        }
      }
      if(has_converged){
        return has_converged;
      }
      for(int iBndGP = 0; iBndGP < numberOfPoints; iBndGP++){

        //!derivative of NR
        dNR[iBndGP]   = -impAndEta[ltsFace].inv_eta_s * (fabs(n_stress[iBndGP]) * dmu_f[iBndGP]) - 1.0;
        //!ratio
        tmp3[iBndGP] = NR[iBndGP]/dNR[iBndGP];

        //!update SRtest
        SRtest[iBndGP] = std::max(AlmostZero,SRtest[iBndGP]-tmp3[iBndGP]);
      }
    }
  }

  /*
 * If the function did not converge it returns false
   * From Upoffc git Zero.ccp
 */
  bool IterativelyInvertSR_Brent(unsigned int ltsFace, int nSRupdates, real LocSR[numberOfPoints],
                            std::array<real, numOfPointsPadded> &LocSV, std::array<real, numOfPointsPadded> &n_stress,
                            std::array<real, numOfPointsPadded> &sh_stress,  std::array<real, numOfPointsPadded> &SRtest ){
    std::function<double(double, int)> F;
    double tol = 1e-30;

    double *RS_a = RS_a_array[ltsFace];
    double RS_sr0_ = m_Params->rs_sr0;
    double invEta = impAndEta[ltsFace].inv_eta_s;

    F = [invEta, &sh_stress, n_stress, RS_a, LocSV, RS_sr0_](double SR, int iBndGP){
      double tmp  = 0.5 / RS_sr0_ *exp(LocSV[iBndGP]/RS_a[iBndGP]) * SR;
      double mu_f  = RS_a[iBndGP] * log(tmp+sqrt(seissol::dr::aux::power(tmp,2)+1.0));
      return -invEta * (fabs(n_stress[iBndGP])*mu_f-sh_stress[iBndGP])-SR;
    };

    //TODO: padded?
    for(int iBndGP = 0; iBndGP < numberOfPoints; iBndGP++){
      //TODO: change boundaries?
      double a = LocSR[iBndGP] - impAndEta[ltsFace].inv_eta_s*sh_stress[iBndGP];
      double b  = LocSR[iBndGP] + impAndEta[ltsFace].inv_eta_s*sh_stress[iBndGP];

      double eps = std::numeric_limits<double>::epsilon();
      double Fa = F(a, iBndGP);
      //if(std::isinf(Fa)){
      //  Fa = std::numeric_limits<double>::max();
      //}
      double Fb = F(b, iBndGP);
      assert(std::copysign(Fa, Fb) != Fa); // Fa and Fb have different signs
      double c = a;
      double Fc = Fa;
      double d = b - a;
      double e = d;
      while (Fb != 0.0) {
        if (std::copysign(Fb, Fc) == Fb) {
          c = a;
          Fc = Fa;
          d = b - a;
          e = d;
        }
        if (std::fabs(Fc) < std::fabs(Fb)) {
          a = b;
          b = c;
          c = a;
          Fa = Fb;
          Fb = Fc;
          Fc = Fa;
        }
        // Convergence test
        double xm = 0.5 * (c - b);
        double tol1 = 2.0 * eps * std::fabs(b) + 0.5 * tol;
        if (std::fabs(xm) <= tol1 || Fb == 0.0) {
          break;
        }
        if (std::fabs(e) < tol1 || std::fabs(Fa) <= std::fabs(Fb)) {
          // bisection
          d = xm;
          e = d;
        } else {
          double s = Fb / Fa;
          double p, q;
          if (a != c) {
            // linear interpolation
            q = Fa / Fc;
            double r = Fb / Fc;
            p = s * (2.0 * xm * q * (q - r) - (b - a) * (r - 1.0));
            q = (q - 1.0) * (r - 1.0) * (s - 1.0);
          } else {
            // inverse quadratic interpolation
            p = 2.0 * xm * s;
            q = 1.0 - s;
          }
          if (p > 0) {
            q = -q;
          } else {
            p = -p;
          }
          if (2.0 * p < 3.0 * xm * q - std::fabs(tol1 * q) && p < std::fabs(0.5 * e * q)) {
            e = d;
            d = p / q;
          } else {
            // bisection
            d = xm;
            e = d;
          }
        }
        a = b;
        Fa = Fb;
        if (std::fabs(d) > tol1) {
          b += d;
        } else {
          b += std::copysign(tol1, xm);
        }
        Fb = F(b, iBndGP);
      }
      SRtest[iBndGP] = b;
    }
    return true;
  }
  /*
   * mu = a * arcsinh[ V/(2*V0) * exp(SV/a) ]
   */
  void updateMu(unsigned int ltsFace, unsigned int iBndGP, real LocSV, real LocSR){
    //! X in Asinh(x) for mu calculation
    real tmp = 0.5 / m_Params->rs_sr0 * exp(LocSV / RS_a_array[ltsFace][iBndGP]) * LocSR;
    //! mu from LocSR
    mu[ltsFace][iBndGP] = RS_a_array[ltsFace][iBndGP] * log(tmp + sqrt(seissol::dr::aux::power(tmp, 2) + 1.0));
  }

  //output time when shear stress is equal to the dynamic stress after rupture arrived
  //currently only for linear slip weakening
  void outputDynamicStress(
      real fullUpdateTime,
      unsigned int face
  ){
    for (int iBndGP = 0; iBndGP < numOfPointsPadded; iBndGP++) {

      if (rupture_time[face][iBndGP] > 0.0 &&
          rupture_time[face][iBndGP] <= fullUpdateTime &&
          DS[iBndGP] &&
          mu[face][iBndGP] <= ( m_Params->mu_w+0.05*(m_Params->rs_f0-m_Params->mu_w) ) ) {
        dynStress_time[face][iBndGP] = fullUpdateTime;
        DS[face][iBndGP] = false;
      }
    }
  }

  virtual void hookSetInitialP_f(std::array<real, numOfPointsPadded> &P_f, unsigned int ltsFace){
    for (int iBndGP = 0; iBndGP < numberOfPoints; iBndGP++) {
      P_f[iBndGP] = 0.0;
    }
  }

  virtual void hookCalcP_f(std::array<real, numOfPointsPadded> &P_f, real LocSlipRate[numberOfPoints], FaultStresses &faultStresses, bool saveTmpInTP, unsigned int iTimeGP, unsigned int ltsFace){
  }



public:
  virtual void evaluate(seissol::initializers::Layer&  layerData,
                        seissol::initializers::DynamicRupture *dynRup,
                        real (*QInterpolatedPlus)[CONVERGENCE_ORDER][tensor::QInterpolated::size()],
                        real (*QInterpolatedMinus)[CONVERGENCE_ORDER][tensor::QInterpolated::size()],
                        real fullUpdateTime,
                        real timeWeights[CONVERGENCE_ORDER],
                        real DeltaT[CONVERGENCE_ORDER]) override {
    //first copy all Variables from the Base Lts dynRup tree
    RateAndStateNucFL103::copyLtsTreeToLocal(layerData, dynRup);

    //!TU 7.07.16: if the SR is too close to zero, we will have problems (NaN)
    //!as a consequence, the SR is affected the AlmostZero value when too small
    double AlmostZero = 1e-45;

    //!PARAMETERS of THE optimisation loops
    //!absolute tolerance on the function to be optimzed
    //! This value is quite arbitrary (a bit bigger as the expected numerical error) and may not be the most adapted
    //! Number of iteration in the loops
    unsigned int nSRupdates = 60;
    unsigned int nSVupdates = 2;
    
    double Gnuc = 0;
    dt = 0;
    for (int iTimeGP = 0; iTimeGP < CONVERGENCE_ORDER; iTimeGP++) {
      dt += DeltaT[iTimeGP];
    }
    if (fullUpdateTime <= m_Params->t_0) {
      Gnuc = Calc_SmoothStepIncrement(fullUpdateTime, dt);
    }

#ifdef _OPENMP
    #pragma omp parallel for schedule(static)
#endif
    for (unsigned ltsFace = 0; ltsFace < layerData.getNumberOfCells(); ++ltsFace) {

      //initialize local variables inside parallel face loop
      bool has_converged = false;
      FaultStresses faultStresses;
      dynamicRupture::kernel::resampleParameter resampleKrnl;
      resampleKrnl.resampleM = init::resample::Values;
      real resampledDeltaStateVar[numberOfPoints];
      real deltaStateVar[numberOfPoints];
      real LocSlipRate[numberOfPoints];
      std::array<real, numOfPointsPadded> tmpSlip{0};   //required for averageSlip calculation
      std::array<real, numOfPointsPadded> normalStress{0};
      std::array<real, numOfPointsPadded> TotalShearStressYZ{0};
      std::array<real, numOfPointsPadded> LocSlipTmp{0};
      std::array<real, numOfPointsPadded> stateVarZero{0};
      std::array<real, numOfPointsPadded> SR_tmp{0};
      std::array<real, numOfPointsPadded> LocSV{0};
      std::array<real, numOfPointsPadded> SRtest{0};

      //for thermalPressure
      std::array<real, numOfPointsPadded> P_f{0};

      precomputeStressFromQInterpolated(faultStresses, QInterpolatedPlus[ltsFace], QInterpolatedMinus[ltsFace], ltsFace);

      if (fullUpdateTime <= m_Params->t_0) {
        //TODO: test padded
        for (int iBndGP = 0; iBndGP < numberOfPoints; iBndGP++) {
          for (int i = 0; i < 6; i++) {
            initialStressInFaultCS[ltsFace][iBndGP][i] += nucleationStressInFaultCS[ltsFace][iBndGP][i] * Gnuc;
          }
        }
      } //end If-Tnuc

      for (int iBndGP = 0; iBndGP < numberOfPoints; iBndGP++) {
        LocSV[iBndGP] = stateVar[ltsFace][iBndGP];     //DISC%DynRup%StateVar(iBndGP,iFace)      //local varriable required
      }

      for (int iTimeGP = 0; iTimeGP < CONVERGENCE_ORDER; iTimeGP++) {
        //TODO: test padded:
        for (int iBndGP = 0; iBndGP < numberOfPoints; iBndGP++) {

          // friction develops as                    mu = a * arcsinh[ V/(2*V0) * exp(SV/a) ]
          // state variable SV develops as          dSV / dt = -(V - L) * (SV - SV_ss)
          //                                        SV_ss = a * ln[ 2*V0/V * sinh(mu_ss/a) ]
          //                                        mu_ss = mu_w + [mu_lv - mu_w] / [ 1 + (V/Vw)^8 ] ^ (1/8) ]
          //                                        mu_lv = mu_0 - (b-a) ln (V/V0)

          TotalShearStressYZ[iBndGP] = std::sqrt(
              seissol::dr::aux::power(initialStressInFaultCS[ltsFace][iBndGP][3] + faultStresses.XYStressGP[iTimeGP][iBndGP], 2) +
              seissol::dr::aux::power(initialStressInFaultCS[ltsFace][iBndGP][5] + faultStresses.XZStressGP[iTimeGP][iBndGP], 2));

          // We use the regularized rate-and-state friction, after Rice & Ben-Zion (1996) //TODO: look up
          // ( Numerical note: ASINH(X)=LOG(X+SQRT(X^2+1)) )
          stateVarZero[iBndGP] = LocSV[iBndGP];    // Careful, the SV must always be corrected using SV0 and not LocSV!

          // The following process is adapted from that described by Kaneko et al. (2008)
          LocSlipRate[iBndGP] = std::sqrt(seissol::dr::aux::power(slipRate1[ltsFace][iBndGP], 2) + seissol::dr::aux::power(slipRate2[ltsFace][iBndGP], 2) );
          LocSlipRate[iBndGP] = std::max(AlmostZero, LocSlipRate[iBndGP]);
          SR_tmp[iBndGP] = LocSlipRate[iBndGP];

        }// End of iBndGP-loop

        hookSetInitialP_f(P_f, ltsFace);

        for (int j = 0; j < nSVupdates; j++) {

          hookCalcP_f(P_f, LocSlipRate, faultStresses, false, iTimeGP, ltsFace);

          //TODO: test for padded:
          for (int iBndGP = 0; iBndGP < numberOfPoints; iBndGP++) {
            //fault strength using LocMu and P_f from previous timestep/iteration
            //1.update SV using Vold from the previous time step
            updateStateVariable(iBndGP, ltsFace, stateVarZero[iBndGP], DeltaT[iTimeGP], SR_tmp[iBndGP], LocSV[iBndGP]);
            normalStress[iBndGP] = faultStresses.NorStressGP[iTimeGP][iBndGP] + initialStressInFaultCS[ltsFace][iBndGP][0] - P_f[iBndGP];
          }// End of iBndGP-loop

          //2. solve for Vnew , applying the Newton-Raphson algorithm
          //effective normal stress including initial stresses and pore fluid pressure
          has_converged = IterativelyInvertSR(ltsFace, nSRupdates, LocSlipRate, LocSV, normalStress, TotalShearStressYZ, SRtest);

          //TODO: test padded
          for (int iBndGP = 0; iBndGP < numberOfPoints; iBndGP++) {

            // 3. update theta, now using V=(Vnew+Vold)/2
            // For the next SV update, use the mean slip rate between the initial guess and the one found (Kaneko 2008, step 6)
            SR_tmp[iBndGP] = 0.5 * (LocSlipRate[iBndGP] + fabs(SRtest[iBndGP]));

            // 4. solve again for Vnew
            LocSlipRate[iBndGP] = fabs(SRtest[iBndGP]);

            //!update LocMu
            updateMu(ltsFace, iBndGP, LocSV[iBndGP], LocSlipRate[iBndGP]);
          }// End of iBndGP-loop
        } //End nSVupdates-loop   j=1,nSVupdates   !This loop corrects SV values


        if (!has_converged) {
          //!logError(*) 'nonConvergence RS Newton', time
          //TODO: error logging : logError(*) 'NaN detected', time
          //std::cout << "nonConvergence RS Newton" << std::endl;
          assert(!std::isnan(LocSlipTmp[0]) && "nonConvergence RS Newton");
        }

        //! 5. get final theta, mu, traction and slip
        hookCalcP_f(P_f, LocSlipRate, faultStresses, true, iTimeGP, ltsFace);

        for (int iBndGP = 0; iBndGP < numberOfPoints; iBndGP++) {
          //! SV from mean slip rate in tmp
          updateStateVariable(iBndGP, ltsFace, stateVarZero[iBndGP], DeltaT[iTimeGP], SR_tmp[iBndGP], LocSV[iBndGP]);

          //!update LocMu for next strength determination, only needed for last update
          updateMu(ltsFace, iBndGP, LocSV[iBndGP], LocSlipRate[iBndGP]);

          //! update stress change
          tracXY[ltsFace][iBndGP] = -((initialStressInFaultCS[ltsFace][iBndGP][3] + faultStresses.XYStressGP[iTimeGP][iBndGP]) / TotalShearStressYZ[iBndGP]) * mu[ltsFace][iBndGP] * normalStress[iBndGP];
          tracXZ[ltsFace][iBndGP] = -((initialStressInFaultCS[ltsFace][iBndGP][5] + faultStresses.XZStressGP[iTimeGP][iBndGP]) / TotalShearStressYZ[iBndGP]) * mu[ltsFace][iBndGP] * normalStress[iBndGP];
          tracXY[ltsFace][iBndGP] -= initialStressInFaultCS[ltsFace][iBndGP][3];
          tracXZ[ltsFace][iBndGP] -= initialStressInFaultCS[ltsFace][iBndGP][5];

          //Compute slip
          //! ABS of LocSR removed as it would be the accumulated slip that is usually not needed in the solver, see linear slip weakening
          slip[ltsFace][iBndGP] += LocSlipRate[iBndGP] * DeltaT[iTimeGP];

          //!Update slip rate (notice that LocSR(T=0)=-2c_s/mu*s_xy^{Godunov} is the slip rate caused by a free surface!)
          slipRate1[ltsFace][iBndGP] = -impAndEta[ltsFace].inv_eta_s * (tracXY[ltsFace][iBndGP] - faultStresses.XYStressGP[iTimeGP][iBndGP]);
          slipRate2[ltsFace][iBndGP] = -impAndEta[ltsFace].inv_eta_s * (tracXZ[ltsFace][iBndGP] - faultStresses.XZStressGP[iTimeGP][iBndGP]);

          //!TU 07.07.16: correct LocSR1_2 to avoid numerical errors
          LocSlipTmp[iBndGP] = sqrt(seissol::dr::aux::power(slipRate1[ltsFace][iBndGP], 2) + seissol::dr::aux::power(slipRate2[ltsFace][iBndGP], 2));
          if (LocSlipTmp[iBndGP] != 0) {
            slipRate1[ltsFace][iBndGP] = LocSlipRate[iBndGP] * slipRate1[ltsFace][iBndGP] / LocSlipTmp[iBndGP];
            slipRate2[ltsFace][iBndGP] = LocSlipRate[iBndGP] * slipRate2[ltsFace][iBndGP] / LocSlipTmp[iBndGP];
          }

          tmpSlip[iBndGP] = tmpSlip[iBndGP] + LocSlipTmp[iBndGP] * DeltaT[iTimeGP];

          slip1[ltsFace][iBndGP] += slipRate1[ltsFace][iBndGP] * DeltaT[iTimeGP];
          slip2[ltsFace][iBndGP] += slipRate2[ltsFace][iBndGP] * DeltaT[iTimeGP];

          //!Save traction for flux computation
          faultStresses.TractionGP_XY[iTimeGP][iBndGP] = tracXY[ltsFace][iBndGP];
          faultStresses.TractionGP_XZ[iTimeGP][iBndGP] = tracXZ[ltsFace][iBndGP];

          deltaStateVar[iBndGP] = LocSV[iBndGP] - stateVar[ltsFace][iBndGP];
        } // End of BndGP-loop
      } // End of iTimeGP-loop

      resampleKrnl.resamplePar = deltaStateVar;
      resampleKrnl.resampledPar = resampledDeltaStateVar;  //output from execute
      resampleKrnl.execute();

      //TODO: test padded
      for (int iBndGP = 0; iBndGP < numberOfPoints; iBndGP++) {
        //write back State Variable to lts tree
        stateVar[ltsFace][iBndGP] = stateVar[ltsFace][iBndGP] + resampledDeltaStateVar[iBndGP];
      }

      // output rupture front
      // outside of iTimeGP loop in order to safe an 'if' in a loop
      // this way, no subtimestep resolution possible
      outputRuptureFront(LocSlipRate, fullUpdateTime, ltsFace);

      calcPeakSlipRate(LocSlipRate, ltsFace);

      //output time when shear stress is equal to the dynamic stress after rupture arrived
      //currently only for linear slip weakening
      outputDynamicStress(fullUpdateTime, ltsFace);

      //---compute and store slip to determine the magnitude of an earthquake ---
      //    to this end, here the slip is computed and averaged per element
      //    in calc_seissol.f90 this value will be multiplied by the element surface
      //    and an output happened once at the end of the simulation
      calcAverageSlip(tmpSlip, ltsFace);

      postcomputeImposedStateFromNewStress(
          QInterpolatedPlus[ltsFace], QInterpolatedMinus[ltsFace],
          faultStresses, timeWeights, ltsFace);

    }//end face loop
  }//end evaluate function
}; //end class Init_FL_103


class seissol::dr::fr_law::RateAndStateThermalFL103 : public seissol::dr::fr_law::RateAndStateNucFL103 {
protected:

  real (*temperature)[numOfPointsPadded];
  real (*pressure)[numOfPointsPadded];
  real (*TP_Theta)[numOfPointsPadded][TP_grid_nz];
  real (*TP_sigma)[numOfPointsPadded][TP_grid_nz];
  real (*TP_half_width_shear_zone)[numOfPointsPadded];
  real (*alpha_hy)[numOfPointsPadded];

  std::array<real, TP_grid_nz> TP_grid{0};
  std::array<real, TP_grid_nz> TP_DFinv{0};

  real Sh[numOfPointsPadded];
  real Theta_tmp[TP_grid_nz];
  real Sigma_tmp[TP_grid_nz];

  //in m_param:
  /*
  real alpha_th;  //= m_Params.alpha_th
  real rho_c; //= m_Params.rho_c
  real TP_Lambda; //= m_Params.TP_lambda
  real temp_0;  //= m_Params.IniTemp
  real pressure_0; //= m_Params.IniPressure
  */

  /*
 * copies all parameters from the DynamicRupture LTS to the local attributes
 */
  void copyLtsTreeToLocal(seissol::initializers::Layer&  layerData,
                          seissol::initializers::DynamicRupture *dynRup) override {
    //first copy all Variables from the Base Lts dynRup tree
    RateAndStateNucFL103::copyLtsTreeToLocal(layerData, dynRup);

    //TODO: change later to const_cast
    seissol::initializers::DR_FL_103_Thermal *ConcreteLts = dynamic_cast<seissol::initializers::DR_FL_103_Thermal *>(dynRup);
    temperature               = layerData.var(ConcreteLts->temperature);
    pressure                  = layerData.var(ConcreteLts->pressure);
    TP_Theta                  = layerData.var(ConcreteLts->TP_theta);
    TP_sigma                  = layerData.var(ConcreteLts->TP_sigma);
    TP_half_width_shear_zone  = layerData.var(ConcreteLts->TP_half_width_shear_zone);
    alpha_hy                  = layerData.var(ConcreteLts->alpha_hy);
  }

  void hookSetInitialP_f(std::array<real, numOfPointsPadded> &P_f, unsigned int ltsFace) override{
    for (int iBndGP = 0; iBndGP < numberOfPoints; iBndGP++) {
        P_f[iBndGP] = pressure[iBndGP][ltsFace];
    }
  }

  void hookCalcP_f(std::array<real, numOfPointsPadded> &P_f, real LocSlipRate[numberOfPoints], FaultStresses &faultStresses, bool saveTmpInTP, unsigned int iTimeGP, unsigned int ltsFace) override {
    for (int iBndGP = 0; iBndGP < numberOfPoints; iBndGP++) {

      Sh[iBndGP] = -mu[ltsFace][iBndGP] * (faultStresses.NorStressGP[iTimeGP][iBndGP] + initialStressInFaultCS[ltsFace][iBndGP][0] - P_f[iBndGP]);

      for (int iTP_grid_nz = 0; iTP_grid_nz < TP_grid_nz; iTP_grid_nz++) {
       //!recover original values as it gets overwritten in the ThermalPressure routine
        Theta_tmp[iTP_grid_nz] = TP_Theta[ltsFace][iBndGP][iTP_grid_nz];
        Sigma_tmp[iTP_grid_nz] = TP_sigma[ltsFace][iBndGP][iTP_grid_nz];
      }
      //!use Theta/Sigma from last call in this update, dt/2 and new SR from NS
      Calc_ThermalPressure(LocSlipRate, iBndGP, ltsFace); //TODO: maybe move iBndGP loop inside this function

      P_f[iBndGP] = pressure[ltsFace][iBndGP];
      if(saveTmpInTP){
        for (int iTP_grid_nz = 0; iTP_grid_nz < TP_grid_nz; iTP_grid_nz++) {
          TP_Theta[ltsFace][iBndGP][iTP_grid_nz] = Theta_tmp[iTP_grid_nz];
          TP_sigma[ltsFace][iBndGP][iTP_grid_nz] = Sigma_tmp[iTP_grid_nz];
        }
      }
    }
  }

  void Calc_ThermalPressure(real LocSlipRate[numberOfPoints] , unsigned int iBndGP, unsigned int ltsFace){
    real tauV, Lambda_prime, tmp, omega, T, p, theta_current, sigma_current;

    T = 0.0;
    p = 0.0;

    for (int iTP_grid_nz = 0; iTP_grid_nz < TP_grid_nz; iTP_grid_nz++) {
      tauV = Sh[iBndGP] * LocSlipRate[iBndGP]; //!fault strenght*slip rate
      Lambda_prime = m_Params->TP_lambda * m_Params->alpha_th / (alpha_hy[ltsFace][iBndGP] - m_Params->alpha_th);
      //!Gaussian shear zone in spectral domain, normalized by w
      tmp = seissol::dr::aux::power(TP_grid[iTP_grid_nz] / TP_half_width_shear_zone[ltsFace][iBndGP], 2);
      //!1. Calculate diffusion of the field at previous timestep

      //!temperature
      theta_current = Theta_tmp[iTP_grid_nz] * exp(-m_Params->alpha_th * dt * tmp);
      //!pore pressure + lambda'*temp
      sigma_current = Sigma_tmp[iTP_grid_nz] * exp(-alpha_hy[ltsFace][iBndGP] * dt * tmp);

      //!2. Add current contribution and get new temperature
      omega = heat_source(tmp, m_Params->alpha_th, iTP_grid_nz);
      Theta_tmp[iTP_grid_nz] = theta_current + (tauV / m_Params->rho_c) * omega;
      omega = heat_source(tmp, alpha_hy[ltsFace][iBndGP], iTP_grid_nz);
      Sigma_tmp[iTP_grid_nz] = sigma_current + ((m_Params->TP_lambda + Lambda_prime) * tauV) / (m_Params->rho_c) * omega;

      //!3. Recover temperature and pressure using inverse Fourier
      //! transformation with the calculated fourier coefficients

      //!new contribution
      T += (TP_DFinv[iTP_grid_nz] / TP_half_width_shear_zone[ltsFace][iBndGP]) * Theta_tmp[iTP_grid_nz];
      p += (TP_DFinv[iTP_grid_nz] / TP_half_width_shear_zone[ltsFace][iBndGP]) * Sigma_tmp[iTP_grid_nz];
    }
    //Update pore pressure change (sigma = pore pressure + lambda'*temp)
    //In the BIEM code (Lapusta) they use T without initial value
    p = p - Lambda_prime*T;

    //Temp and pore pressure change at single GP on the fault + initial values
    temperature[iBndGP][ltsFace] = T + m_Params->IniTemp;
    pressure[iBndGP][ltsFace] = -p + m_Params->IniPressure;
  }

  real heat_source(real tmp, real alpha, unsigned int iTP_grid_nz){
    //!original function in spatial domain
    //!omega = 1/(w*sqrt(2*pi))*exp(-0.5*(z/TP_half_width_shear_zone).^2);
    //!function in the wavenumber domain *including additional factors in front of the heat source function*
    //!omega = 1/(*alpha*Dwn**2**(sqrt(2.0*pi))*exp(-0.5*(Dwn*TP_half_width_shear_zone)**2)*(1-exp(-alpha**dt**tmp))
    //!inserting Dwn/TP_half_width_shear_zone (scaled) for Dwn cancels out TP_half_width_shear_zone
    return 1.0/(alpha*tmp*(sqrt(2.0*M_PI)))*exp(-0.5*seissol::dr::aux::power(TP_grid[iTP_grid_nz], 2))*(1.0 - exp(-alpha*dt*tmp));
  }
};

class seissol::dr::fr_law::SolverRateAndStateVwFL7 : public seissol::dr::fr_law::BaseFrictionSolver {
protected:
  //Attributes
  real  (*stateVar)[numOfPointsPadded];
  real*                   RS_sl0;
  real*                   RS_a;
  /*
 * copies all parameters from the DynamicRupture LTS to the local attributes
 */
  void copyLtsTreeToLocal(seissol::initializers::Layer&  layerData,
                          seissol::initializers::DynamicRupture *dynRup) override {
    //first copy all Variables from the Base Lts dynRup tree
    BaseFrictionSolver::copyLtsTreeToLocal(layerData, dynRup);
    //TODO: change later to const_cast
    //seissol::initializers::DR_lts_template *ConcreteLts = dynamic_cast<seissol::initializers::DR_lts_template *>(dynRup);

    seissol::initializers::DR_FL_3 *ConcreteLts = dynamic_cast<seissol::initializers::DR_FL_3 *>(dynRup);
    stateVar                        = layerData.var(ConcreteLts->StateVar);
    RS_sl0                          = layerData.var(ConcreteLts->RS_sl0);
    RS_a                            = layerData.var(ConcreteLts->RS_a);

    /*a
     * Add new LTS parameter specific for this
     */
  }



public:
  virtual void evaluate(seissol::initializers::Layer&  layerData,
                        seissol::initializers::DynamicRupture *dynRup,
                        real (*QInterpolatedPlus)[CONVERGENCE_ORDER][tensor::QInterpolated::size()],
                        real (*QInterpolatedMinus)[CONVERGENCE_ORDER][tensor::QInterpolated::size()],
                        real fullUpdateTime,
                        real timeWeights[CONVERGENCE_ORDER],
                        real DeltaT[CONVERGENCE_ORDER]) override {
    SolverRateAndStateVwFL7::copyLtsTreeToLocal(layerData, dynRup);
#ifdef _OPENMP
#pragma omp parallel for schedule(static)
#endif
    for (unsigned ltsFace = 0; ltsFace < layerData.getNumberOfCells(); ++ltsFace) {
      //initialize struct for in/outputs stresses
      FaultStresses faultStresses{};

      //declare local variables
      real LocSlipRate[numberOfPoints];

      //compute stresses from Qinterpolated
      precomputeStressFromQInterpolated(faultStresses, QInterpolatedPlus[ltsFace], QInterpolatedMinus[ltsFace], ltsFace);

      /*
      double RS_a;    //DISC%DynRup%RS_a  !< RS constitutive parameter "a", direct effect
      double RS_sl0;  //DISC%DynRup%RS_sl0     !< Reference slip  , Dc, char. lengt scale
      */

      //initialize local variables
      real LocSlip, LocSlip1, LocSlip2, LocSR1, LocSR2;
      real LocSV;
      real P_0;
      real LocP;
      real time_inc;
      real P;
      real ShTest;
      real SV0;
      real Tc;
      real coeft;
      int nSRupdates, nSVupdates;
      real SRtest;
      real tmp;
      real NR, dNR;
      real LocMu;
      real LocTracXY, LocTracXZ;

      //TODO: test padded?
      for (int iBndGP = 0; iBndGP < numberOfPoints; iBndGP++) {

        LocSlip = slip[ltsFace][iBndGP]; //DISC%DynRup%Slip(iBndGP,iFace)              //!< Slip path at given fault node
        LocSlip1 = slip1[ltsFace][iBndGP]; //DISC%DynRup%Slip1(iBndGP,iFace)            //!< Slip at given fault node along loc dir 1
        LocSlip2 = slip2[ltsFace][iBndGP]; //DISC%DynRup%Slip2(iBndGP,iFace)            // !< Slip at given fault node along loc dir 2
        LocSR1 = slipRate1[ltsFace][iBndGP]; //DISC%DynRup%SlipRate1(iBndGP,iFace)         // !< Slip Rate at given fault node
        LocSR2 = slipRate2[ltsFace][iBndGP]; //DISC%DynRup%SlipRate2(iBndGP,iFace)         // !< Slip Rate at given fault node
        LocSV = stateVar[ltsFace][iBndGP];     //DISC%DynRup%StateVar(iBndGP,iFace)
        P_0 = initialStressInFaultCS[ltsFace][iBndGP][0]; //EQN%InitialStressInFaultCS[iBndGP][1][iFace];
        for (int iTimeGP = 0; iTimeGP < CONVERGENCE_ORDER; iTimeGP++) {
          LocP = faultStresses.NorStressGP[iTimeGP][iBndGP];
          time_inc = DeltaT[iTimeGP];


          // load traction and normal stress
          P = LocP + P_0;
          ShTest = std::sqrt(
              seissol::dr::aux::power(initialStressInFaultCS[ltsFace][iBndGP][3] + faultStresses.XYStressGP[iTimeGP][iBndGP], 2) +
                  seissol::dr::aux::power(initialStressInFaultCS[ltsFace][iBndGP][5] + faultStresses.XZStressGP[iTimeGP][iBndGP], 2));

          // We use the regularized rate-and-state friction, after Rice & Ben-Zion (1996)
          // ( Numerical note: ASINH(X)=LOG(X+SQRT(X^2+1)) )

          SV0 = LocSV;    // Careful, the SV must always be corrected using SV0 and not LocSV!

          // The following process is adapted from that described by Kaneko et al. (2008)
          nSRupdates = 5; //TODO: can be put outside of loop
          nSVupdates = 2;

          LocSlipRate[iBndGP] = std::sqrt(seissol::dr::aux::power(LocSR1, 2) + seissol::dr::aux::power(LocSR2, 2)); //can be put outside of the loop

          //charact. time scale Tc
          Tc = RS_sl0[ltsFace] / m_Params->rs_sr0;
          // exponent
          coeft= exp(-time_inc / Tc);

          for (int j = 0; j < nSVupdates; j++) { //!This loop corrects SV values
            LocSlipRate[iBndGP] = abs(LocSlipRate[iBndGP]);
            //exact integration assuming constant V in this loop
            LocSV=Tc*LocSlipRate[iBndGP]*(1.0-coeft) + coeft*SV0;
            //! Newton-Raphson algorithm to determine the value of the slip rate.
            //! We wish to find SR that fulfills g(SR)=f(SR), by building up the function NR=f-g , which has
            //!  a derivative dNR = d(NR)/d(SR). We can then find SR by iterating SR_{i+1}=SR_i-( NR_i / dNR_i ).
            //! In our case we equalize the values of the traction for two equations:
            //!             g = SR*mu/2/cs + T^G             (eq. 18 of de la Puente et al. (2009))
            //!             f = (mu*P_0-|S_0|)*S_0/|S_0|     (Coulomb's model of friction)
            //!             where mu = mu_s + a V/(V+Vc) - b SV/(SV + Vc)
            //!
            SRtest = LocSlipRate[iBndGP];   // We use as first guess the SR value of the previous time step
            for (int i = 0; i < nSRupdates; i++) {   //!This loop corrects SR values
              tmp = m_Params->rs_f0+ RS_a[ltsFace] *SRtest/(SRtest+m_Params->rs_sr0)-m_Params->rs_b*LocSV/(LocSV+RS_sl0[ltsFace]); //=mu
              NR = - impAndEta[ltsFace].inv_eta_s * (abs(P)*tmp-ShTest)-SRtest;
              dNR          = -impAndEta[ltsFace].inv_eta_s *
                             (abs(P)*(RS_a[ltsFace]/(SRtest+m_Params->rs_sr0)-RS_a[ltsFace]*SRtest/seissol::dr::aux::power(SRtest+m_Params->rs_sr0,2))) -1.0;
              SRtest = SRtest-NR/dNR;
            }   // End nSRupdates-Loop
            tmp=0.5*(LocSlipRate[iBndGP]+abs(SRtest));  // For the next SV update, use the mean slip rate between the initial guess and the one found (Kaneko 2008, step 6)
            LocSlipRate[iBndGP]=abs(SRtest);
          }   // End nSVupdates-Loop -  This loop corrects SV values

          LocSV    = Tc*tmp*(1-coeft) + coeft*SV0;
          tmp = 0.5 * (LocSlipRate[iBndGP])/m_Params->rs_sr0 * exp((m_Params->rs_f0 + m_Params->rs_b*log(m_Params->rs_sr0*LocSV/RS_sl0[ltsFace])) / RS_a[ltsFace]);

          //! Ampuero and Ben-Zion 2008 (eq. 1):
          // LocMu = friction coefficient (mu_f)
          // RS_f0 = static coefficient (mu_s)
          // RS_a = positive coefficient, quantifying  the direct effect (alpha)
          // LocSR = slip rate (V)
          // RS_sr0 = characteristic velocity scale (V_c)
          // RS_b = positive coefficient, quantifying  the evolution effect (beta)
          // RS_sl0 = characteristic velocity scale (V_c)
          LocMu = m_Params->rs_f0+RS_a[ltsFace]*LocSlipRate[iBndGP]/(LocSlipRate[iBndGP]+m_Params->rs_sr0)-m_Params->rs_b*LocSV/(LocSV+RS_sl0[ltsFace]);

          // update stress change
          LocTracXY = -((initialStressInFaultCS[ltsFace][iBndGP][3] + faultStresses.XYStressGP[iTimeGP][iBndGP]) / ShTest) *LocMu * P;
          LocTracXZ = -((initialStressInFaultCS[ltsFace][iBndGP][5] + faultStresses.XZStressGP[iTimeGP][iBndGP]) / ShTest) *LocMu * P;
          LocTracXY = LocTracXY - initialStressInFaultCS[ltsFace][iBndGP][3];
          LocTracXZ = LocTracXZ - initialStressInFaultCS[ltsFace][iBndGP][5];

          // Compute slip
          LocSlip = LocSlip + LocSlipRate[iBndGP] * time_inc; // ABS of LocSR removed as it would be the accumulated slip that is usually not needed in the solver, see linear slip weakening

          //Update slip rate (notice that LocSR(T=0)=-2c_s/mu*s_xy^{Godunov} is the slip rate caused by a free surface!)
          LocSR1 = -impAndEta[ltsFace].inv_eta_s  * (LocTracXY - faultStresses.XYStressGP[iTimeGP][iBndGP]);
          LocSR2 = -impAndEta[ltsFace].inv_eta_s  * (LocTracXZ - faultStresses.XZStressGP[iTimeGP][iBndGP]);

          LocSlip1 = LocSlip1 + (LocSR1) * time_inc;
          LocSlip2 = LocSlip2 + (LocSR2) * time_inc;


          //Save traction for flux computation
          faultStresses.TractionGP_XY[iTimeGP][iBndGP] = LocTracXY;
          faultStresses.TractionGP_XZ[iTimeGP][iBndGP] = LocTracXZ;
        } //End of iTimeGP loop

        mu[ltsFace][iBndGP] = LocMu;
        slipRate1[ltsFace][iBndGP] = LocSR1;
        slipRate2[ltsFace][iBndGP] = LocSR2;
        slip[ltsFace][iBndGP] = LocSlip;
        slip1[ltsFace][iBndGP] = LocSlip1;
        slip2[ltsFace][iBndGP] = LocSlip2;
        stateVar[ltsFace][iBndGP] = LocSV;
        tracXY[ltsFace][iBndGP] = LocTracXY;
        tracXZ[ltsFace][iBndGP] = LocTracXZ;
      }//End of iBndGP-loop

      // output rupture front
      // outside of iTimeGP loop in order to safe an 'if' in a loop
      // this way, no subtimestep resolution possible
      outputRuptureFront(LocSlipRate, fullUpdateTime, ltsFace);

      //output peak slip rate
      calcPeakSlipRate(LocSlipRate, ltsFace);

      //save stresses in imposedState
      postcomputeImposedStateFromNewStress(QInterpolatedPlus[ltsFace], QInterpolatedMinus[ltsFace], faultStresses, timeWeights, ltsFace);

    }//End of Loop over Faces

  }//End of Function evaluate
};




#endif //SEISSOL_DR_SOLVER_RATE_AND_STATE_H
