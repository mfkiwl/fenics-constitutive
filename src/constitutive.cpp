#include <pybind11/eigen.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <interfaces.h>
#include <laws.h>
#include <local_damage.h>

namespace py = pybind11;

PYBIND11_MODULE(cpp, m)
{
    // This was created with the help of
    //     https://pybind11.readthedocs.io/en/stable/basics.html
    // and
    //     https://pybind11.readthedocs.io/en/stable/classes.html

    m.doc() = "Super awesome, super fast constitutive lib";

    pybind11::enum_<Constraint>(m, "Constraint")
            .value("UNIAXIAL_STRAIN", Constraint::UNIAXIAL_STRAIN)
            .value("UNIAXIAL_STRESS", Constraint::UNIAXIAL_STRESS)
            .value("PLANE_STRAIN", Constraint::PLANE_STRAIN)
            .value("PLANE_STRESS", Constraint::PLANE_STRESS)
            .value("FULL", Constraint::FULL)
            .value("3D", Constraint::FULL);

    m.def("g_dim", &Dim::G);
    m.def("q_dim", &Dim::Q);

    pybind11::class_<IpBase> ipBase(m, "IpBase");
    ipBase.def("evaluate", &IpBase::evaluate, py::arg("strain"), py::arg("i") = 0);
    ipBase.def("update", &IpBase::update, py::arg("strain"), py::arg("i") = 0);
    ipBase.def("resize", &IpBase::resize, py::arg("n"));


    pybind11::class_<LinearElastic, IpBase> linearElastic(m, "LinearElastic");
    linearElastic.def(pybind11::init<double, double, Constraint>());

    pybind11::class_<Base> base(m, "Base");
    base.def(pybind11::init<IpBase&>());
    base.def("evaluate", &Base::evaluate);
    base.def("update", &Base::update);
    base.def("resize", &Base::resize);
    base.def_readwrite("stress", &Base::_stress);
    base.def_readwrite("dstress", &Base::_dstress);

    pybind11::class_<ModMisesEeq> modMises(m, "ModMisesEeq");
    modMises.def(pybind11::init<double, double, Constraint>());
    modMises.def("evaluate", &ModMisesEeq::Evaluate);

    pybind11::class_<LocalDamage, IpBase> localDamage(m, "LocalDamage");
    localDamage.def(pybind11::init<double, double, Constraint, double, double, double, double>());
    localDamage.def("evaluate_kappa", &LocalDamage::EvaluateKappa);

    pybind11::class_<LawInterface> law(m, "LawInterface");

    pybind11::class_<GradientDamage, LawInterface> gdm(m, "GradientDamage");
    gdm.def(pybind11::init<double, double, Constraint, double, double, double, double>());
    gdm.def("kappa", &GradientDamage::Kappa);

    pybind11::class_<LocalDamageNew, LawInterface> local(m, "LocalDamageNew");
    local.def(pybind11::init<double, double, Constraint, double, double, double, double>());
    local.def("kappa", &LocalDamageNew::Kappa);

    pybind11::class_<LinearElasticNew, LawInterface> linearElasticNew(m, "LinearElasticNew");
    linearElasticNew.def(pybind11::init<double, double, Constraint>());

    pybind11::enum_<Q>(m, "Q")
            .value("SIGMA", Q::SIGMA)
            .value("DSIGMA_DEPS", Q::DSIGMA_DEPS)
            .value("EEQ", Q::EEQ)
            .value("DEEQ", Q::DEEQ)
            .value("DSIGMA_DE", Q::DSIGMA_DE);

    pybind11::class_<IpLoop> ipLoop(m, "IpLoop");
    ipLoop.def(pybind11::init<>());
    ipLoop.def("add_law", &IpLoop::AddLaw);
    ipLoop.def("evaluate", &IpLoop::Evaluate);
    ipLoop.def("update", &IpLoop::Update);
    ipLoop.def("resize", &IpLoop::Resize);
    ipLoop.def("get", &IpLoop::Get);
    ipLoop.def("required_inputs", &IpLoop::RequiredInputs);
}