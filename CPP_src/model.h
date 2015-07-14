#ifndef model_H
#define model_H

class UNI{
public:
	double a,b,w,pi;
	
	int st;
	//sufficient stats
	double ri_forward, ri_reverse; //current responsibility
	double r_forward, r_reverse; //running total
	double delta_a, delta_b;
	UNI();
	UNI(double, double, double, int);
	double pdf(double,int);	
	string print();

};
double sum(double * , int );
double LOG(double );


class EMG{
public:
	double mu, si, l, pi, w;
	//sufficient stats
	double ri_forward, ri_reverse; //current responsibility
	double ey, ex, ex2, r_forward, r_reverse;//running total
	EMG();
	EMG(double, double, double, double, double);
	double pdf(double,int);
	double EY(double ,int);
	double EY2(double ,int);
	string print();
};

class NOISE{
public:
	double a, b, w, pi;
	double ri_forward, ri_reverse; //current responsibility
	double r_forward, r_reverse; //running total
	double pdf(double, int);
	NOISE();
	NOISE(double, double, double, double);
};


class component{
public:
	EMG bidir;
	UNI forward;
	UNI reverse;
	NOISE noise; 

	bool type;

	//=====================================
	//parameters to simulate from
	double alpha_0, alpha_1, alpha_2, beta_0, beta_1, beta_2;
	//=====================================
	//bayesian priors for parameters, MAP
	//FOR SIGMA ; variance in loading, gamma
	double ALPHA_0, BETA_0;
	//FOR LAMBA ; rate of initiation, gamma
	double ALPHA_1, BETA_1;
	//FOR W ; weight , dirchlet
	double ALPHA_2;
	//FOR PI ; strand prob. , beta
	double ALPHA_3;




	component();
	void initialize(double, double , double, int , double , double, double);

	double evaluate(double, int);
	void add_stats(double, double , int, double);
	void update_parameters(double,int);
	double get_all_repo();
	bool check_elongation_support();
	void print();
	void reset();
	string write_out();
};


class classifier{
public:
	int K; //number of components
	double convergence_threshold; //convergence check
	int max_iterations; //stop after this many iterations
	bool seed; //seed with a gross peak finder
	double noise_max; //fit a uniform noise component, never let it get above this weight
	double move; //variance in moving the uniform supports
	//===================================================================================
	//Bayesian Priors
	double p;
	int fit(segment *,vector<double>);
	classifier(int, double, int, double, double, double);
	classifier();
	void free_classifier();
	string print_out_components();
	//===================================================================================
	//final important parameters
	double ll,pi;
	double last_diff;
	component * components;
	int resets;
	bool converged;
	double r_mu;
};




#endif