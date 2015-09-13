#include "model.h"
#include "load.h"
#include "template_matching.h"
#include <math.h> 
#include <limits>
#include <iostream>
#include <algorithm>
#include <unistd.h>
#include <random>


//=============================================
//Helper functions
double IN(double x){ //Standard Normal PDF 
	return exp(-pow(x,2)*0.5)/sqrt(2*M_PI);
	
}
double IC(double x){ //Standard Normal CDF
	return 0.5*(1+erf(x/sqrt(2)));
}


double R(double x){ //Mills Ratio
	if (x > 8){
		return 1.0 / x;
	}
	double N = IC(x);
	double D = IN(x);
	if (D < pow(10,-15)){ //machine epsilon
		return 1.0 / pow(10,-15);
	}
	return exp(log(1. - N)-log(D));
}

bool checkNumber(double x){
	if (isfinite(x)){
		return true;
	}
	return false;

}
//=============================================
// Uniform Noise Class
double NOISE::pdf(double x, int strand){
	if (strand == 1){
		return (w*pi) / abs(b-a);
	}
	return (w*(1-pi)) / abs(b-a);
}
NOISE::NOISE(){}
NOISE::NOISE(double A, double B, double W, double PI){
	a=A;
	b=B;
	w=W;
	pi=PI;
}

//=============================================
//Uniform Class
double UNI::pdf(double x, int strand){ //probability density function
	double p;

	if ( a<= x and x <=b){

		p= w / abs(b- a);
		p= p*pow(pi, max(0, strand) )*pow(1.-pi, max(0, -strand) );
		return p;
	}
	return 0;
}

string UNI::print(){
	string text = ("U: " + to_string(a) + "," + to_string(b) 
	+ "," + to_string(w) + "," + to_string(pi));
	return text;
}	
UNI::UNI(double start, double stop, double w_i, int strand, int POS, double Pi){
	a 		= start;
	b 		= stop;
	w 		= w_i;
	st 		= strand;
	pos 	= POS;
	if (st==1){
		pi=1;
	}else{
		pi=0;
	}
	//===================
	//this oversets the constraint that uniform must take either 
	//forward or reverse data points
	pi 		= Pi;
	//===================
	delta_a=0;
	delta_b=0;
	ri_forward=0, ri_reverse=0;


}

UNI::UNI(){} //empty constructor


//=============================================
//Exponentially Modified Gaussian class

string EMG::print(){
	string text 	= ("N: " + to_string(mu)+","+to_string(si)
		+ "," + to_string(l) + "," + to_string(w) + "," + to_string(pi));
	return text;
}

EMG::EMG(){}//empty constructor

EMG::EMG(double MU, double SI, double L, double W, double PI ){
	mu 	= MU;
	si 	= SI;
	l  	= L;
	w 	= W;
	pi 	= PI;
}


double EMG::pdf(double z, int s ){
	double vl 		= (l/2)*(s*2*(mu-z) + l*pow(si,2));
	double p;
	if (vl > 100){ //potential for overflow, inaccuracies
		p 			= l*IN((z-mu)/si)*R(l*si - s*((z-mu)/si));
	}else{
		p 			= (l/2)*exp(vl)*erfc((s*(mu-z) + l*pow(si ,2))/(sqrt(2)*si));
	}
	vl 				= p*w*pow(pi, max(0, s) )*pow(1.-pi, max(0, -s) );
	if (checkNumber(vl)){
		return vl;
	}
	return 0.;

}
double EMG::EY(double z, int s){
	return max(0. , s*(z-mu) - l*pow(si, 2) + (si / R(l*si - s*((z-mu)/si))));
}
double EMG::EY2(double z, int s){

	return pow(l,2)*pow(si,4) + pow(si, 2)*(2*l*s*(mu-z)+1 ) + pow(mu-z,2) - ((si*(l*pow(si,2) + s*(mu-z)))/R(l*si - s*((z-mu)/si) ));
	
}

//=========================================================
//components wrapper class for EMG and UNIFORM objects

component::component(){//empty constructor
} 

void component::set_priors(double s_0, double s_1, 
	double l_0, double l_1, double w_0,double strand_0){
	//============================
	//for sigma
	alpha_0 	= 20.46;
	beta_0 		= 10.6;
	//============================
	//for lambda
	alpha_1 	= 20.823;
	beta_1 		= 0.5;
	//==============================
	//for initial length of Uniforms
	alpha_2 	= 1.297;
	beta_2 		= 8260;

	//*****************************************************
	//Priors on parameters for MAP Estimate
	ALPHA_0 = s_0, BETA_0 =s_1; //for sigma
	ALPHA_1 = l_0, BETA_1 =l_1; //for lambda
	ALPHA_2 = w_0; //for weights, dirchlet
	ALPHA_3 = strand_0; //for strand probs

	
}

int get_nearest_position(segment * data, double center, double dist){
	int i;

	if (dist < 0 ){
		i=0;
		while (i < data->XN and (data->X[0][i] -center) < dist){
			i++;
		}
	}else{
		i=data->XN-1;
		while (i >=0 and (data->X[0][i] - center) > dist){
			i--;
		}
	}
	return i;
}
bool check_uniform_support(component c, int forward){
	if (forward==1){
		if (c.forward.b < (c.bidir.mu + c.bidir.si + (1.0 / c.bidir.l) )){
			return false;
		}
		return true;
	}
	else{
		if (c.reverse.a < (c.bidir.mu - c.bidir.si - (1.0 / c.bidir.l) )){
			return false;
		}
		return true;

	}
}


void component::initialize(double mu, segment * data , int K, double scale, double noise_w, 
	double noise_pi){//random seeds...
	EXIT=false;
	if (noise_w>0){
		noise 	= NOISE(data->minX, data->maxX, 
			noise_w, noise_pi);
		type 	= 0; 
	}else{
		//====================================
		random_device rd;
		mt19937 mt(rd());
		
		double sigma,lambda, pi_EMG, w_EMG  ;	
		double b_forward,  w_forward;
		double a_reverse,  w_reverse;

		//====================================
		//start sampling
		//for the bidirectional/EMG component
		gamma_distribution<double> dist_sigma(alpha_0,beta_0);
		gamma_distribution<double> dist_lambda(alpha_1,beta_1);
		uniform_real_distribution<double> dist_lambda_2(1, 500);
		uniform_real_distribution<double> dist_sigma_2(1, 50);
		gamma_distribution<double> dist_lengths(1,( (data->maxX-data->minX)/(K)));
		
		sigma 		= dist_sigma_2(mt)/scale;
		lambda 		= scale/dist_lambda_2(mt) ;
		double dist = (1.0/lambda) + sigma + dist_lengths(mt);
		int j 		= get_nearest_position(data, mu, dist);
		
		b_forward 	= data->X[0][j];
		if (b_forward < (mu+(1.0/lambda)) ){
			forward 	= UNI(mu+(1.0/lambda), data->maxX, 0., 1, j, 0.5);
		}
		else{	
			forward 	= UNI(mu+(1.0/lambda), b_forward, 1.0 / (3*K), 1, j, 0.5);
		}	
		dist 		= (-1.0/lambda) - sigma - dist_lengths(mt);
		j 			= get_nearest_position(  data, mu, dist);
		
		a_reverse 	= data->X[0][j];

		bidir 		= EMG(mu, sigma, lambda, 1.0 / (3*K), 0.5);
		if (a_reverse > mu-(1.0/lambda) ){
			reverse 	= UNI(data->minX, mu-(1.0/lambda), 0., -1, j,0.5);
		}else{
			reverse 	= UNI(a_reverse, mu-(1.0/lambda), 1.0 / (3*K), -1, j,0.5);
		}
		type 		= 1;
	}

} 

void component::print(){
	if (type==1){
		string text 	= bidir.print()+ "\n";
		text+=forward.print()+ "\n";
		text+=reverse.print() + "\n";
		cout<<text;
	}else{
		cout<<"NOISE: " << noise.w<<"," <<noise.pi<<endl;
	}
}

string component::write_out(){
	if (type==1){
		string text 	= bidir.print()+ "\n";
		text+=forward.print()+ "\n";
		text+=reverse.print() + "\n";

		return text;
	}
	return "";
}



double component::evaluate(double x, int st){
	if (type ==0){ //this is the uniform noise component
		return noise.pdf(x, st);
	}
	if (st==1){
		bidir.ri_forward 	= bidir.pdf(x, st);
		forward.ri_forward 	= forward.pdf(x, st);
		reverse.ri_forward 	= reverse.pdf(x,st);
		return bidir.ri_forward + forward.ri_forward + reverse.ri_forward;
	}
	bidir.ri_reverse 	= bidir.pdf(x, st);
	reverse.ri_reverse 	= reverse.pdf(x, st);
	forward.ri_reverse 	= forward.pdf(x, st);
	return bidir.ri_reverse + reverse.ri_reverse + forward.ri_reverse;
}
double component::pdf(double x, int st){
	if (type==0){
		return noise.pdf(x,st);
	}
	if (st==1){
		return bidir.pdf(x,st) + forward.pdf(x, st);
	}
	return bidir.pdf(x,st) + reverse.pdf(x,st);
}


void component::add_stats(double x, double y, int st, double normalize){
	if (type==0){//noise component
		if (st==1){
			noise.r_forward+=(y*noise.ri_forward/normalize);
			noise.ri_forward=0;
		}else{
			noise.r_reverse+=(y*noise.ri_reverse/normalize);
			noise.ri_reverse=0;
		}

	}else{
		double vl, vl2, vl3;
		if (st==1){
			vl 	= bidir.ri_forward / normalize;
			vl2 = forward.ri_forward/normalize;
			vl3 = reverse.ri_forward/normalize;
			bidir.ri_forward=0, forward.ri_forward=0;
			bidir.r_forward+=(vl*y);
			forward.r_forward+=(vl2*y);
			reverse.r_forward+=(vl3*y);
		
		}else{
			vl 	= bidir.ri_reverse / normalize;
			vl2 = reverse.ri_reverse / normalize;
			vl3 = forward.ri_reverse / normalize;
			bidir.ri_reverse=0, reverse.ri_reverse=0;
			bidir.r_reverse+=(vl*y);

			reverse.r_reverse+=(vl2*y);
			forward.r_reverse+=(vl3*y);
		}
		//now adding all the conditional expections for the convolution
		double current_EY 	= bidir.EY(x, st);
		double current_EY2 	= bidir.EY2(x, st);
		double current_EX 	= x-(st*current_EY);
		bidir.ey+=current_EY*vl*y;
		bidir.ex+=current_EX*vl*y;
		bidir.ex2+=(pow(current_EX,2) + current_EY2 - pow(current_EY,2))*vl*y;	
	}
	
}

void component::reset(){
	if (type){
		bidir.ey=0, bidir.ex=0, bidir.ex2=0, bidir.r_reverse=0, bidir.r_forward=0;
		bidir.ri_forward=0, forward.ri_forward=0, forward.ri_reverse=0;
		bidir.ri_reverse=0, reverse.ri_reverse=0, reverse.ri_forward=0;
		forward.r_forward=0, forward.r_reverse=0, reverse.r_reverse=0, reverse.r_forward=0;
		forward.delta_a=0, forward.delta_b=0, reverse.delta_a=0, reverse.delta_b=0;
	}else{
		noise.r_forward=0,noise.r_reverse=0;
		noise.ri_reverse=0,noise.ri_forward=0 ;
		
	}
}

double component::get_all_repo(){
	if (type==1){
		return bidir.r_forward+bidir.r_reverse+forward.r_forward+reverse.r_reverse;
	}
	return noise.r_forward+noise.r_reverse;

}

void component::update_parameters(double N, int K){
	if (type==1){
		//first for the bidirectional
		double r 	= bidir.r_forward + bidir.r_reverse;
		bidir.pi 	= (bidir.r_forward + ALPHA_3) / (r + ALPHA_3*2);
		bidir.w 	= (r + ALPHA_2) / (N + ALPHA_2*K*3 + K*3) ;
		bidir.mu 	= bidir.ex / (r+0.001);

		
		bidir.si 	= pow(abs((1. /(r + 3 + ALPHA_0 ))*(bidir.ex2-2*bidir.mu*bidir.ex + 
			r*pow(bidir.mu,2) + 2*BETA_0  )), 0.5) ;
		if (bidir.si > 10){
			EXIT 	= true;
		}
		bidir.l 	= min((r+ALPHA_1) / (bidir.ey + ALPHA_1), 4.);
		if (bidir.l < 0.05 ){
			EXIT 	= true;
		}
		//now for the forward and reverse strand elongation components
		forward.w 	= (forward.r_forward + ALPHA_2) / (N+ ALPHA_2*K*3 + K*3);
		reverse.w 	= (reverse.r_reverse + ALPHA_2) / (N+ ALPHA_2*K*3 + K*3);
		forward.a 	= bidir.mu + (1.0 /bidir.l), reverse.b=bidir.mu - (1.0 / bidir.l);
		//update PIS, this is obviously overwritten if we start the EM seeder with 0/1
		forward.pi 	= (forward.r_forward + 1) / (forward.r_forward + forward.r_reverse+2);
		reverse.pi 	= (reverse.r_forward + 1)/ (reverse.r_forward + reverse.r_reverse+2);


	}
}

bool component::check_elongation_support(){
	if (forward.b <=forward.a and bidir.mu==0){
		return true;
	}
	else if(reverse.b <= reverse.a and bidir.mu==0){
		return true;
	}
	return false;
}


void component::initialize_with_parameters(vector<double> init_parameters, segment * data, 
	int K, double left, double right){
	if (init_parameters.empty()){
		noise 	= NOISE(data->minX, data->maxX, 0.01, 0.5);
		type 	= 0; 
	}else{
		double mu 	= init_parameters[0];
		double si 	= init_parameters[1];
		double l 	= init_parameters[2];
		double pi 	= init_parameters[3];
		bidir 		= EMG(mu, si, l, 1.0 / (3*K), pi);//bidir component
		//now choose supports of forward and reverse
		random_device rd;
		mt19937 mt(rd());
		uniform_real_distribution<double> left_move( left , mu + (1.0/l) );
		uniform_real_distribution<double> right_move( mu-(1.0/l ), right );
			
		forward 	= UNI(mu+(1.0/l), right_move(mt) , 1. / double(K), 1, 0, 1);
				
		reverse 	= UNI(left_move(mt), mu-(1.0/l ), 1. / double(K), -1, 0,0.);
		type 		= 1;
	}	
}

void component::initialize_with_parameters2(vector<double> init_parameters, segment * data, 
	int K, double left, double right){
	if (init_parameters.empty()){
		noise 	= NOISE(data->minX, data->maxX, 0.01, 0.5);
		type 	= 0; 
	}else{
		double mu 	= init_parameters[0];
		double si 	= init_parameters[1];
		double l 	= init_parameters[2];
		double pi 	= init_parameters[3];
		bidir 		= EMG(mu, si, l, 1.0 / (3*K), pi);//bidir component
		//now choose supports of forward and reverse
		
		forward 	= UNI(mu+(1.0/l), right , 1. / double(3*K), 1, 0, 1);
				
		reverse 	= UNI(left, mu-(1.0/l ), 1. / double(3*K), -1, 0,0.);
		type 		= 1;
	}	
}


//=========================================================
//FIT function this where we are running EM rounds
//=========================================================
//helper functions for fit

double sum(double * X, int N){
	double vl=0;
	for (int i = 0; i < N; i++){
		vl+=X[i];
	}
	return vl;

}
double LOG(double x){
	if (x <= 0){
		return nINF;
	}
	return log(x);
}


double calc_log_likelihood(component * components, int K, segment * data){
	double ll 	= 0;
	double forward, reverse;
	for (int i = 0 ; i < data->XN; i++){
		forward=0, reverse=0;
		for (int k = 0; k < K; k++){
			forward+=(components[k].pdf(data->X[0][i], 1));
			reverse+=(components[k].pdf(data->X[0][i], -1));
		}
		ll+=LOG(forward)*data->X[1][i]; 
		ll+=LOG(reverse)*data->X[2][i];
	}
	return ll;
}	
int get_direction(uniform_int_distribution<int> direction,mt19937 mt){
	if (direction(mt)==0){
		return 1;
	}
	return -1;
}
int get_new_position(geometric_distribution<int> dist_uni, mt19937 mt, 
	int pos, int N, int direction, int st, segment * data){
	int ct 	= dist_uni(mt)+1;
	int i = pos;
	int j = 0;

	if (direction==1){
		// while (i < (N-1) and j < ct){
		// 	if (data->X[st][i] > 0){
		// 		j++;
		// 	}
		// 	i++;
		// }
		i 	= min(N-1, i+ct);
	}else{
		// while (i > 0 and j < ct){
		// 	if (data->X[st][i]>0){
		// 		j++;
		// 	}
		// 	i--;
		// }
		i 	= max(0, i-ct);
	}
	//printf("old: %d,new: %d, change: %d,old: %f, new: %f\n", pos,i, ct*direction, data->X[0][pos], data->X[0][i] );
	return i;
}

double move_uniforom_support(component * components, int K, int add, 
	segment * data, double move, double base_ll){
	//===========================================================================
	//normal distribution centered around 0 and some variance, how much to move 
	//uniform supports
	random_device rd;
	mt19937 mt(rd());
	geometric_distribution<int> dist_uni(0.9);
	uniform_int_distribution<int> direction(0,1);
	int 	steps[K][2];
	double  new_bounds[K][2];
	double ll;
	double prev_a, prev_b;
	for (int k = 0; k < K; k++){
		prev_b=components[k].forward.b, prev_a=components[k].reverse.a;
		steps[k][0] 	= get_new_position(dist_uni, mt, 
			components[k].forward.pos, data->XN, get_direction(direction, mt), 1, data);
		steps[k][1] 	= get_new_position(dist_uni, mt, 
			components[k].reverse.pos, data->XN, get_direction(direction, mt), 2, data);
		//firs the forward
		components[k].forward.b=data->X[0][steps[k][0]];
		if (check_uniform_support(components[k], 1)){
			ll 	= calc_log_likelihood(components, K+add, data);
			//printf("%f,%f,%f,%f\n", prev_b, components[k].forward.b, base_ll,ll );
			if (ll > base_ll){
				new_bounds[k][0] 	= components[k].forward.b;
			}else{
				new_bounds[k][0] 	= prev_b;
				steps[k][0] 		= components[k].forward.pos;
			}
		}else{
			new_bounds[k][0] 	= prev_b;
			steps[k][0] 		= components[k].forward.pos;
		}
		components[k].forward.b 	= prev_b;
		//now reverse
		components[k].reverse.a=data->X[0][steps[k][1]];
		if (check_uniform_support(components[k], 0)){

			ll 	= calc_log_likelihood(components, K+add, data);
			if (ll > base_ll){
				new_bounds[k][1] 	= components[k].reverse.a;
			}else{
				new_bounds[k][1] 	= prev_a;
				steps[k][1] 		= components[k].reverse.pos;
			}
			//printf("%f,%f,%f,%f\n", prev_a, components[k].reverse.a, base_ll,ll );
		}else{
				new_bounds[k][1] 	= prev_a;
				steps[k][1] 		= components[k].reverse.pos;			
		}
		components[k].reverse.a 	= prev_a;
	}
	for (int k =0; k < K;k++){
		components[k].forward.pos 	= steps[k][0];
		components[k].forward.b 	= new_bounds[k][0];
		components[k].reverse.pos 	= steps[k][1];
		components[k].reverse.a 	= new_bounds[k][1];
 		
			
	}



	ll 		= calc_log_likelihood(components, K+add, data);
	return ll;
}

void update_weights_only(component * components, segment * data, int K, int add){
	
	double EX[K+add][3];
	double current[K+add][3][2];
	double x,f,r, normed_f, normed_r, N,W;
	for (int k =0; k < K+add; k++){
		EX[k][0]=0,EX[k][1]=0,EX[k][2]=0;
		current[K+add][0][0]=0, current[K+add][1][0]=0, current[K+add][2][0]=0;

	}
	for (int i =0; i< data->XN; i++){
		x 	= data->X[0][i], f 	=data->X[1][i], r=data->X[2][i];
		normed_f = 0, normed_r 	= 0;
		for (int k = 0; k<K; k++){
			current[k][0][0] 	= components[k].bidir.pdf(x,1);
			current[k][1][0] 	= components[k].forward.pdf(x,1);
			current[k][2][0] 	= components[k].reverse.pdf(x,1);
			current[k][0][1] 	= components[k].bidir.pdf(x,-1);			
			current[k][1][1] 	= components[k].forward.pdf(x,-1);
			current[k][2][1] 	= components[k].reverse.pdf(x,-1);
			
			normed_f+=(current[k][0][0]+ current[k][1][0] + current[k][2][0]);
			normed_r+=(current[k][0][1]+ current[k][1][1] + current[k][2][1]);	
		}
		if (add){
			current[K][0][0] 	= components[K].noise.pdf(x,1);
			current[K][0][1] 	= components[K].noise.pdf(x,-1);
			normed_f+=current[K][0][0];
			normed_r+=current[K][0][1];			
		}
		for (int k =0; k < K+add; k++){

			if (normed_f){
				if (k < K){
					EX[k][0]+=f*(current[k][0][0]/normed_f);
					EX[k][1]+=f*(current[k][1][0]/normed_f);
					EX[k][2]+=f*(current[k][2][0]/normed_f);
				}else{
					EX[k][0]+=f*(current[k][0][0]/normed_f);
				}
			}
			if (normed_r){
				if (k < K){
					EX[k][0]+=r*(current[k][0][1]/normed_r);
					EX[k][1]+=r*(current[k][1][1]/normed_r);
					EX[k][2]+=r*(current[k][2][1]/normed_r);
				}else{
					EX[k][0]+=r*(current[k][0][1]/normed_r);		
				}
			}
		}
	}
	N 	= 0;
	W 	= 0;
	for (int k = 0; k < K+add; k++){
		N+=(EX[k][0]+EX[k][1]+EX[k][2]);
	}
	for (int k = 0; k < K; k++){
		components[k].bidir.w 		= EX[k][0] / N;
		components[k].forward.w 	= EX[k][1] / N;
		components[k].reverse.w 	= EX[k][2] / N;
		W+=(components[k].bidir.w + components[k].forward.w + components[k].reverse.w  );
	}
	for (int k = 0; k < K; k++){
		components[k].bidir.w 		= components[k].bidir.w / W;
		components[k].forward.w 	= components[k].forward.w / W;
		components[k].reverse.w 	= components[k].reverse.w / W;
	}
}



double move_uniforom_support2(component * components, int K, int add, 
	segment * data, double move, double base_ll, int direction, double var, 
	vector<double> left, vector<double> right){
	//====================================
	random_device rd;
	mt19937 mt(rd());
	normal_distribution<double> step(0,var);
	//====================================
	int N 	 	= data->XN;
	double ll 	= nINF;
	double oldb, olda;
	for (int c = 0; c < K; c++){
		oldb  	= components[c].forward.b;
		olda 	= components[c].reverse.a;
		components[c].forward.b+=step(mt);
		components[c].forward.b=min(right[c],components[c].forward.b );
		if (components[c].forward.b < (components[c].bidir.mu + (1./components[c].bidir.l)  )  ){
			components[c].forward.b 	= components[c].bidir.mu + (2./components[c].bidir.l)  ;
		}
		ll 		= calc_log_likelihood(components, K+1, data  );
		if (ll < base_ll){
			components[c].forward.b 	= oldb;
		}else{
			base_ll 	= ll;
		}
		components[c].reverse.a+=step(mt);
		components[c].reverse.a = max(components[c].reverse.a, left[c]);
		ll 		= calc_log_likelihood(components, K+1, data  );
		if (ll < base_ll){
			components[c].reverse.a 	= olda;
		}else{
			base_ll 	= ll;
		}
		
	}	
	return base_ll;
			
}


//=========================================================
//For Classifier class / wrapper around EM

classifier::classifier(int k, double ct, int mi, double nm,
	double R_MU, double alpha_0, double beta_0,
	double alpha_1, double beta_1, double alpha_2,double alpha_3){
	K 						= k ;
	seed 					= true;
	convergence_threshold 	= ct;
	max_iterations 			= mi;
	noise_max 				= nm;
	p 						= 0.8;
	last_diff 				= 0;
	r_mu 					= R_MU;

	//=============================
	//hyperparameters
	ALPHA_0=alpha_0, BETA_0=beta_0, ALPHA_1=alpha_1, BETA_1=beta_1;
	ALPHA_2=alpha_2, ALPHA_3=alpha_3;

	move_l = true;

}
classifier::classifier(int k, double ct, int mi, double nm,
	double R_MU, double alpha_0, double beta_0,
	double alpha_1, double beta_1, double alpha_2,double alpha_3, bool MOVE){
	K 						= k ;
	seed 					= true;
	convergence_threshold 	= ct;
	max_iterations 			= mi;
	noise_max 				= nm;
	p 						= 0.8;
	last_diff 				= 0;
	r_mu 					= R_MU;

	//=============================
	//hyperparameters
	ALPHA_0=alpha_0, BETA_0=beta_0, ALPHA_1=alpha_1, BETA_1=beta_1;
	ALPHA_2=alpha_2, ALPHA_3=alpha_3;
	move_l 	= MOVE;
}
classifier::classifier(double ct, int mi, double nm,
	double R_MU, double alpha_0, double beta_0,
	double alpha_1, double beta_1, double alpha_2,double alpha_3, vector<vector<double>> IP){
	seed 					= true;
	convergence_threshold 	= ct;
	max_iterations 			= mi;
	noise_max 				= nm;
	p 						= 0.8;
	last_diff 				= 0;
	r_mu 					= R_MU;

	//=============================
	//hyperparameters
	ALPHA_0=alpha_0, BETA_0=beta_0, ALPHA_1=alpha_1, BETA_1=beta_1;
	ALPHA_2=alpha_2, ALPHA_3=alpha_3;
	init_parameters 		= IP;
	
}




classifier::classifier(){};//empty constructor

int classifier::fit(segment * data, vector<double> mu_seeds){
	//=========================================================================
	//for resets
	random_device rd;
	mt19937 mt(rd());
	uniform_real_distribution<double> dist_uni(data->minX,data->maxX);
	
	double l 	= data->maxX - data->minX;
	pi 	= sum(data->X[1], data->XN)/ data->N;
	double vl 	= 1.0 / l;
	if (K==0){
		//calc_likeihood coming from uniform model, only
		ll 	= 0;
		double SS 	= 0;
		for (int i = 0; i < data->XN; i ++){
			ll+=(LOG(vl*(pi) )*data->X[1][i]);
			ll+=(LOG(vl*(1-pi))*data->X[2][i]);
			SS+=data->X[1][i];
		}
		if (not isfinite(ll) or ll==nINF){
			printf("%f, %f, %f\n",SS ,data->minX, data->maxX );
		}
		converged=true;
		last_diff=0;
		components 	= new component[1];
		components[K].initialize(0., data, 0., 0. , noise_max, pi);
		return 1;
	}
	int add 	= noise_max>0;
       
	components 	= new component[K+add];

	//initialize components
	for (int k = 0; k < K; k++){
		components[k].set_priors(ALPHA_0, BETA_0, ALPHA_1, BETA_1, ALPHA_2, ALPHA_3);
	}

	//===========================================================================
	//random seeds, initialize
	int i 	= 0;
	double mu;
	
	for (int k = 0; k < K; k++){
		if (mu_seeds.size()>0){
			i 	= sample_centers(mu_seeds ,  p);
			mu 	= mu_seeds[i];
			if (r_mu > 0){
				normal_distribution<double> dist_r_mu(mu, r_mu);
				mu 		= dist_r_mu(mt);
			}
		}else{
			mu 			= dist_uni(mt);
		}
		components[k].initialize(mu, data, K, data->SCALE , 0., 0.);
		if (mu_seeds.size() > 0){
			mu_seeds.erase (mu_seeds.begin()+i);	
		}
	}
       
	if (add){
		components[K].initialize(0., data, 0., 0. , noise_max, pi);
	}
 		
	//===========================================================================
	int t 			= 0; //EM loop ticker
	double prevll 	= nINF; //previous iterations log likelihood
	converged 		= false; //has the EM converged?
	double norm_forward, norm_reverse,N; //helper variables
	while (t < max_iterations && not converged){
		
		//******
		//reset old sufficient statistics
		for (int k=0; k < K+add; k++){
			components[k].reset();
			if (components[k].EXIT){
				converged=false, ll=nINF;
				return 0;
			}
		       
		}
		
		//******
		//E-step
		for (int i =0; i < data->XN;i++){
			norm_forward=0;
			norm_reverse=0;
			for (int k=0; k < K+add; k++){
				if (data->X[1][i]){//if there is actually data point here...
					norm_forward+=components[k].evaluate(data->X[0][i],1);
				}
				if (data->X[2][i]){//if there is actually data point here...
					norm_reverse+=components[k].evaluate(data->X[0][i],-1);
				}
			}
			//now we need to add the sufficient statistics
			for (int k=0; k < K+add; k++){
				if (norm_forward){
					components[k].add_stats(data->X[0][i], data->X[1][i], 1, norm_forward);
				}
				if (norm_reverse){
					components[k].add_stats(data->X[0][i], data->X[2][i], -1, norm_reverse);
				}
			}

		
		}
		//******
		//M-step
		//get normalizing constant
		N=0;
		for (int k = 0; k < K+add; k++){
			N+=(components[k].get_all_repo());
		}
		//update the new parameters
		for (int k = 0; k < K+add; k++){
			components[k].update_parameters(N, K);
		}
		

		ll 	= calc_log_likelihood(components, K+add, data);
		//******
		//Move Uniform support		
		if (move_l){
			ll 	= move_uniforom_support(components, K, add, data,move, ll);
		}
		if (abs(ll-prevll)<convergence_threshold){
			for (int k = 0; k < K; k++){
				double std 	= (components[k].bidir.si + (1.0 / components[k].bidir.l)) ;
				if (std > 10){
					ll 		= nINF;	
				}
			}	
		
			converged=true;
		}
		if (not isfinite(ll)){
			ll 	= nINF;
			return 0;	
		}
		last_diff=abs(ll-prevll);

		prevll=ll;
		// for (int c = 0; c<K;c++){
		// 	components[c].print();
		// }

		t++;
	}
	for (int k = 0; k < K; k++){
		double std 	= (components[k].bidir.si + (1.0 / components[k].bidir.l)) ;
		if (std > 10){
			ll 		= nINF;	
		}
	}	


	return 1;
}
vector<vector<double>> sort_mus(vector<vector<double>> X){
	bool changed=true;

	while (changed){
		changed=false;
		for (int i = 0; i < X.size()-1; i++  )	{
			if (X[i][0] > X[i+1][0]){ //sort by starting position
				vector<double> copy 	= X[i];
				X[i] 					= X[i+1];
				X[i+1] 					= copy;
				changed=true;
			}
		}
	}
	return X;
}

int classifier::fit_uniform_only(segment * data){
	//so in this case, there are a set of bidirectionals that are set and we are going to just try and maximize their 
	//elongation support first (and then maybe restimate parameters?)
	int K 			= init_parameters.size();
	init_parameters  = sort_mus(init_parameters);
	components 	= new component[K+1];
	//printf("--BEFORE--\n");
	vector<double> left;
	vector<double> right;
	double MU, L;
	left.push_back(data->minX);
	for (int k = 0; k < K; k++){
		MU 	= init_parameters[k][0], L 	= init_parameters[k][2];
		if (k > 0){
			right.push_back(MU);
		}
		if ( k < K-1 ){
			left.push_back(MU);
		}
	}
	right.push_back(data->maxX);

	for (int k =0; k < K;k++){
		components[k].initialize_with_parameters(init_parameters[k], data, K, left[k], right[k]);
		//components[k].print();
	}
			
	vector<double> empty;
	components[K].initialize_with_parameters(empty, data, K, data->minX,data->maxX);

	bool converged=false;
	int t=0;
	ll 	= calc_log_likelihood(components, K+1, data  );
	double prevll 	= nINF;
	int times 		= 0;
	int changes 	= 0;
	double var 		= 10;

	while (not converged and t < max_iterations){
		
		update_weights_only(components, data, K, 1);
		ll 	= calc_log_likelihood(components, K+1, data  );
		ll 	= move_uniforom_support2( components, K, 1, data, move,  ll, 1, var, left, right);
		//update_weights_only(components, data, K, 1);
		//ll 	= calc_log_likelihood(components, K+1, data  );
		
		//printf("%f\n",ll );
		if (abs(ll-prevll) < convergence_threshold){
			changes++;
		}else{
			changes=0;
		}
		if (changes >20){
			converged=true;
		}

		prevll=ll;

		t++;
	}
	//now we can probably remaximize...but maybe save that for a later date...


	return 1;

}
int classifier::fit_uniform_only2(segment * data){
	

	int K 			= init_parameters.size();
	init_parameters  = sort_mus(init_parameters);
	
	//vector<vector<double>> init_parameters;
	typedef vector<vector<double>>::iterator ip_type;
	int count 		= 0;
	int IN 			= init_parameters.size();
	vector<vector<double>>BOUNDS;
	for (ip_type p = init_parameters.begin(); p!=init_parameters.end(); p++){
		//find best split points on the foward/reverse strands
		int i,j,k, l,t, argj, argl;
		double std 		= (1./(*p)[2]) + (*p)[1];
		double forward_a = (*p)[0] + std*1.5;
		double reverse_b = (*p)[0] - std*1.5;
		//first thing is to get i and k
		i= 0, k =0;
		double prev_dist_i, prev_dist_k,dist_i,dist_k, bound_forward, bound_reverse, left_vl, right_vl;
		prev_dist_i=INF, prev_dist_k = INF;
		while (i < data->XN and k < data->XN){

			dist_i 	= abs(data->X[0][i] - forward_a);
			dist_k 	= abs(data->X[0][k] - reverse_b);
			if (prev_dist_k > dist_k ){
				k++;
			}
			if (prev_dist_i > dist_i){
				i++;
			}
			if (prev_dist_k <= dist_k and prev_dist_i <= dist_i  ){
				break;
			}
			prev_dist_i=dist_i, prev_dist_k = dist_k;

		}
		//now pick best split_point
		if (count==0){
			bound_reverse 	= data->minX;
		}else{
			bound_reverse 	= init_parameters[count][0];
		}
		if (count+1 >= IN){
			bound_forward 	= data->maxX;
		}else{
			bound_forward 	= init_parameters[count+1][0];
		}
		bool FIRST_TIME = true;
		double left_N, right_N,w, current_ll, max_ll;
		max_ll 	= nINF;
		j 	= i+1;
		argj = j;
		while (j < data->XN and data->X[0][j] < bound_forward){
			left_vl 	= 1. / (data->X[0][j] -data->X[0][i]  );
			right_vl  	= 1. / (bound_forward -data->X[0][j]  );
			if (FIRST_TIME){
				left_N=0, right_N=0;
				t 	= i;
				while (t < data->XN and t < j){
					left_N+=data->X[1][t];
					t++;
				}
				t 	= j;
				while (t < data->XN and data->X[0][t] < bound_forward){
					right_N+=data->X[1][t];
					t++;

				}
				FIRST_TIME=false;
			}else{
				left_N+=data->X[1][j];
				right_N-=data->X[1][j];
			}
			w 	= left_N / (left_N + right_N);
			left_vl*=w;
			right_vl*=(1-w);
			current_ll 	= log(left_vl)*left_N + log(right_vl)*right_N;
			if (current_ll > max_ll){
				max_ll 	= current_ll;
				argj 	= j;
			}
			j++;
		}
		FIRST_TIME = true;
		l 	= k-1;
		left_N=0, right_N=0;
		max_ll 	= nINF;
		argl = l;
		while (l >= 0 and data->X[0][l] > bound_reverse){
			right_vl  	= 1. / abs(data->X[0][k] -data->X[0][l]  );
			left_vl  	= 1. / abs(data->X[0][l] - bound_reverse);
			if (FIRST_TIME){
				left_N=0, right_N=0;
				t 	= k;
				while (t >= 0 and t > l){
					right_N+=data->X[2][t];
					t--;
				}
				t 	= l;
				while (t >= 0 and data->X[0][t] > bound_reverse){
					left_N+=data->X[2][t];
					t--;

				}
			//	printf("%f,%d, %f, %f\n",left_N, l, data->X[0][l], bound_reverse );
				FIRST_TIME=false;
			}else{
				left_N-=data->X[2][l];
				right_N+=data->X[2][l];
			}
			if (left_N==0){
				break;
			}
			//printf("%f,%f\n", left_N, right_N );
			w 	= left_N / (left_N + right_N);
//			w 	= 0.1;
			left_vl*=w;
			right_vl*=(1.-w);
			current_ll 	= log(left_vl)*left_N + log(right_vl)*right_N;
			//printf("%f\n", current_ll );
			if (current_ll 	> max_ll){
				max_ll 	= current_ll;
				argl 	= l;
			}

			l--;
		}

		vector<double> current_bounds(2);
		if (argj < data->XN){
			current_bounds[0] 	= data->X[0][argj];
		}else{
			current_bounds[0] 	= data->maxX;	
		}
		if (argl >=0){
			current_bounds[1] 	= data->X[0][argl];
		}else{
			current_bounds[1] 	= data->minX;	
		}
		count++;
		BOUNDS.push_back(current_bounds);
	}

	components 	= new component[K+1];
	for (int k =0; k < K;k++){
		components[k].initialize_with_parameters2(init_parameters[k], data, K, BOUNDS[k][1], BOUNDS[k][0]);
	}
	vector<double> empty;
	components[K].initialize_with_parameters(empty, data, K, data->minX,data->maxX);


	// //fit weights?
	int t 	= 0;
	double prevll 	= nINF;
	bool 	converged =false;
	while (not converged and t < max_iterations){
		
		update_weights_only(components, data, K, 1);
		ll 	= calc_log_likelihood(components, K+1, data  );
		if (abs(ll-prevll) < convergence_threshold){
			converged=true;
		}
		prevll=ll;
		t++;
	}
	
	return 1;

}
string classifier::print_out_components(){
	string text 	= "";
	for (int k = 0; k < K; k++){
		text+=components[k].write_out();
	}
	return text;
}














