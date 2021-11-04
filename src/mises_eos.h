#pragma once
#include "interfaces.h"
#include "plasticity.h"
#include <tuple>
#include <eigen3/Eigen/Dense>
#include <math.h>
#include <complex>

struct EOS{
    virtual double Evaluate(double eta, double e) = 0;
};

class MisesEOS: public LawInterface
{
public:
    std::shared_ptr<EOS> _eos;
    std::vector<QValues> _internal_vars_0;
    std::vector<QValues> _internal_vars_1;
    Eigen::VectorXd T_vol;
    Eigen::MatrixXd T_dev;
    double _mu;
    double _rho0;
    //std::unordered_map<std::string, double> parameters;
    MisesEOS(double mu, double rho0, double H, std::shared_ptr<EOS> eos)
        : _mu(mu), _rho0(rho0), _eos(eos) 
    {
        _internal_vars_0.resize(Q::LAST);
        _internal_vars_1.resize(Q::LAST);

        _internal_vars_0[LAMBDA] = QValues(1);
        _internal_vars_1[LAMBDA] = QValues(1);

        _internal_vars_0[E] = QValues(1);
        _internal_vars_1[E] = QValues(1);

        _internal_vars_0[RHO] = QValues(1);
        _internal_vars_1[RHO] = QValues(1);

        T_dev.resize(6,6);
        T_vol.resize(6);
        T_dev <<
                2./3., -1./3., -1./3., 0., 0., 0.,
                -1./3., 2./3., -1./3., 0., 0., 0.,
                -1./3., -1./3., 2./3., 0., 0., 0.,
                0., 0., 0., 1., 0., 0.,
                0., 0., 0., 0., 1., 0.,
                0., 0., 0., 0., 0., 1.;
        T_vol << 1./3.,1./3.,1./3.,0.,0.,0.;


    }

    void DefineOutputs(std::vector<QValues>& output) const override
    {
        output[SIGMA] = QValues(6);
    }

    void DefineInputs(std::vector<QValues>& input) const override
    {
        input[L] = QValues(3,3);
        input[SIGMA] = QValues(6);
        input[TIME_STEP] = QValues(1);
    }
    Eigen::VectorXd GetInternalVar(Q which)
    {
        return _internal_vars_0.at(which).data;
    }
    std::complex<double> Yield(double lam, std::complex<double> del_lam)
    {
        
    }
    void Evaluate(const std::vector<QValues>& input, std::vector<QValues>& output, int i) override
    {
        int maxit = 10;
        Eigen::Matrix3d L_ = input[L].Get(i);
        const Eigen::VectorXd sigma_n = input[SIGMA].Get(i);
        auto h = input[TIME_STEP].GetScalar(i);
        auto lambda = _internal_vars_0[LAMBDA].GetScalar(i);
        auto energy = _internAL_VARS_0[E].GetScalar(i);
        const auto D_ = 0.5 * (L_ + L_.transpose());
        const auto W_ = 0.5 * (L_ - L_.transpose());
        const auto d_eps = matrix_to_mandel(D_);

        auto stress = mandel_to_matrix(sigma_n);
        stress += 0.5 * h * (stress * W_.transpose() + W_ * stress);

        /***********************************************************************
         * START CONSTITUTIVE MODEL HERE
         * 1) Calculate failure surface Y_failure
         * 2) Calculate Yield surface Y_yield = f(Y_failure)
         **********************************************************************/
        double p_n = T_vol.dot(sigma_n);
        auto s_n = T_dev * matrix_to_mandel(stress);
        auto s_tr = s_n + 2. * _param[RHT_SHEAR] * T_dev * d_eps * h;
        double s_tr_eq = sqrt(1.5 * s_tr.transpose() * s_tr);
        double alpha = 0.0;

        double del_lam = 0.0;
        double complex_step = 1e-10;
        std::complex<double> ih(0.,complex_step);
        auto Y_y = Yield(lambda, del_lam);
        if (s_tr_eq >= Y_y.real()){
            //plastic flow initiated
            double f = 0.0;
            double df = 0.0;
            int j = 0;
            do  {
                //calculate yield surface with complex step
                Y_y = Yield(lambda, del_lam+ih);

                f = s_tr_eq - 3.*_mu * del_lam - Y_y.real();
                df =  3.*_mu + Y_y.imag()/complex_step;
                del_lambda += del_lambda - f/df;

                j++;
            } while (abs(f)> 1e-10 && j < maxit);
            
            alpha = (1. - 3.*_mu * del_lambda / s_tr_eq);

        } else {
            //elastic
            alpha = 1.0;
        }

        //Update deviatoric stress s
        auto s = alpha * s_tr;
        
        /***********************************************************************
         * END CONSTITUTIVE MODEL HERE
         **********************************************************************/

        /***********************************************************************
         * UPDATE DENSITY
         * The density is updated using the explicit midpoint rule for the
         * deformation gradient.
         **********************************************************************/
        auto factor_1 = Eigen::MatrixXd::Identity(3,3)-0.5*h*L_;
        auto factor_2 = Eigen::MatrixXd::Identity(3,3)+0.5*h*L_;
        _internal_vars_1[RHO].Set(_internal_vars_0[RHO].GetScalar(i) * factor_1.determinant() / factor_2.determinant(), i);
        /***********************************************************************
         * UPDATE ENERGY AND EOS
         **********************************************************************/
        auto rho_12 = 0.5 * (_internal_vars_0[RHO].GetScalar(i) + _internal_vars_1[RHO].GetScalar(i));
        auto eta = _internal_vars_1[RHO].GetScalar(i)/_rho0 - 1.;
        const auto d_eps_vol = T_vol.dot(d_eps);
        const Eigen::VectorXd s_12 = 0.5*(T_dev * sigma_n + s);
        auto e0 = _internal_vars_0[E].GetScalar(i);
        auto e1 = e0;
        auto e_tilde = e0 + (h/rho) * (s_12.dot(T_dev * d_eps)-0.5*_internal_vars_0[RHO] * d_eps_vol);
        do{
            e0 = e1;
            e1 = e_tilde - 0.5*(h/rho)*d_eps_vol*_eos->Evaluate(eta ,e0);
        } while (std::abs(e1-e0)>1e-10);

        auto p = _eos->Evaluate(eta, e1);
        _internal_vars_1[E].Set(e1, i);
        /***********************************************************************
         * Combine deviatoric and volumetric stresses and use stress rate
         **********************************************************************/

        stress = mandel_to_matrix(s + T_vol * p);
        
        stress += 0.5 * h * (stress * W_.transpose() + W_ * stress);
        
        output[SIGMA].Set(matrix_to_mandel(stress),i);
        _internal_vars_1[LAMBDA].Set(_internal_vars_0[LAMBDA].GetScalar(i)+del_lambda, i);
    }


    void Update(const std::vector<QValues>& input, int i) override
    {
        _internal_vars_0[E].Set(_internal_vars_1[E].GetScalar(i),i);
        _internal_vars_0[LAMBDA].Set(_internal_vars_1[LAMBDA].GetScalar(i), i);
        _internal_vars_0[RHO].Set(_internal_vars_1[RHO].GetScalar(i), i);
    }

    void Resize(int n) override
    {
        for (auto& qvalues : _internal_vars_0)
            qvalues.Resize(n);

        for (auto& qvalues : _internal_vars_1)
            qvalues.Resize(n);
    }

};
