// Implementation of Polynomial Optics [2012] for Nuke11.


// TODO

// I think there might be a problem with the guard function which locks code to a single thread
// Whole of polynomial optics code is run in that
// Check if #pragma actually works or not..



// This is the name that Nuke will use to store this operator in the
// scripts. So that nuke can locate the plugin, this must also be the
// name of the compiled plugin (with .machine added to the end):
static const char* const CLASS = "polynomialOpticsBlur";

// This is the text displayed by the [?] button in the control panel:
static const char* const HELP = "Implementation of Polynomial Optics [2012]. Re-traces an image through a lens system.";

////////////////////////////////////////////////////////////////


#include "DDImage/Iop.h"
#include "DDImage/Row.h"
#include "DDImage/Tile.h"
#include "DDImage/Knobs.h"
#include "DDImage/Thread.h"
using namespace DD::Image;

// polynomial optics
#include "paper/TruncPoly/TruncPolySystem.hh"
#include "paper/OpticalElements/OpticalMaterial.hh"
#include "paper/OpticalElements/Spherical5.hh"
#include "paper/OpticalElements/Cylindrical5.hh"
#include "paper/OpticalElements/Propagation5.hh"
#include "paper/OpticalElements/TwoPlane5.hh"
#include "paper/OpticalElements/FindFocus.hh"

#define cimg_display 0
#include "paper/include/CImg.h"
#include "paper/include/spectrum.h"
using namespace cimg_library;
// end polynomial optics

#include <iostream>
#include <stdlib.h>
#include <math.h>


// Disable some warnings on Microsoft VC++ compilers.
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable:4305)
#endif




// Define the C++ class that is the new operator.
class polynomialOpticsBlur : public Iop {
  // These are the locations the user interface will store into:
  double sample_mul;
  int num_lambdas;
  double focusDistance;
  double pupilRadius;

  bool _firstTime; 
  Lock _lock;
  CImg <float> * img_out;

public: 
  // You must implement these functions:
  polynomialOpticsBlur(Node*);
  void _validate(bool);
  void _open();
  void _request(int, int, int, int, ChannelMask, int);
  void engine(int y, int x, int r, ChannelMask, Row &);

  virtual void knobs(Knob_Callback);

  const char* Class() const { return CLASS; }
  const char* node_help() const { return HELP; } 

  static const Description d;

  // You may need to implement the destructor if you allocate memory:
  //~polynomialOpticsBlur();

private:
  Transform4f get_system(float, float,  int = 3);

};


// The constructor must initialize the user controls to their default values:
polynomialOpticsBlur::polynomialOpticsBlur(Node* node) : Iop(node){
  sample_mul = 100.0f;
  num_lambdas = 12;
  focusDistance = 5000000; // 5000000 for 5 km
  pupilRadius = 19.5f;

  _firstTime = true;
}


// The knobs function describes each user control so the user interface
// can be built. The possible controls are listed in DDImage/Knobs.h:
void polynomialOpticsBlur::knobs(Knob_Callback f){
  // WH_knob(f, &width, "size");
  Int_knob(f, &num_lambdas, "Lambdas");
  Float_knob(f, &sample_mul, "Samples");
  Float_knob(f, &focusDistance, "Object Distance");
  Float_knob(f, &pupilRadius, "Pupil Radius");
}


// This is a function that creates an instance of the operator, and is needed for the Iop::Description to work:
static Iop* polynomialOpticsBlur_c(Node* node) {
  return new polynomialOpticsBlur(node);
}

// The Iop::Description is how Nuke knows what the name of the operator is,
// how to create one, and the menu item to show the user. The menu item
// is ignored in recent versions of Nuke, but you might want to set it anyway.
const Iop::Description polynomialOpticsBlur::d(CLASS, "Filter/polynomialOpticsBlur", polynomialOpticsBlur_c);


void polynomialOpticsBlur::_open()
{
  _firstTime = true;
}

// When the operator runs, "open" is called first. This function
// copies the "info" data from the input operator(s), modifies it
// according to what this operator does, and saves this information so
// that operators connected to this one can see it. The boolean flag
// is so that Nuke can call a "fast" and "slow" version. The fast
// version should avoid opening any files and just make the best
// guess. The slow (argument==true) version will always be called
// before request() or engine() are called:
void polynomialOpticsBlur::_validate(bool for_real){
  copy_info(); // copy bbox channels etc from input0, which will validate it.
}

// After open is done, "request" is called. This is passed a "viewport"
// which describes which channels and a rectangular "bounding box" that will
// be requested. The operator must translate this to the area that will
// be requested from it's inputs and call request() on them. If you don't
// implement this then a default version requests the entire area of
// pixels and channels available from the input. If possible you should
// override this so that the resulting caches are smaller, this also helps
// Nuke display what portions of the tree are being used.
void polynomialOpticsBlur::_request(int x, int y, int r, int t, ChannelMask channels, int count){
  // request all input input as we are going to search the whole input area
  ChannelSet readChannels = input0().info().channels();
  input(0)->request( readChannels, count );
}

// This is the operator that does all the work. For each line in the area
// passed to request(), this will be called. It must calculate the image data
// for a region at vertical position y, and between horizontal positions
// x and r, and write it to the passed row structure. Usually this works
// by asking the input for data, and modifying it:
void polynomialOpticsBlur::engine ( int y, int l, int r, ChannelMask channels, Row& row )
{
  {
    Guard guard(_lock);


    // Compute whole image using PO only once
    if (_firstTime) {
      std::cout << "First time " << std::endl;

      // get input properties
      Format format = input0().format();
      const int fx = format.x();
      const int fy = format.y();
      const int fr = format.r();
      const int ft = format.t();

      const int height = ft - fy ;
      const int width = fr - fx ;

      // the framebuffer we save the input in
      CImg<float> img_in(width, height, 1, 3, 0);


      ChannelSet readChannels = input0().info().channels();
      Interest interest( input0(), fx, fy, fr, ft, readChannels, true );
      interest.unlock();

      // get input pixel data and store in CImg buffer
      for (int ry = fy; ry < ft; ry++) {
        Row row(fx, fr);
        row.get(input0(), ry, fx, fr, readChannels);

        if (aborted()){
          // ?? should I add this ?? 
          // _lock.unlock();
          return;
        }

        foreach(z, channels){
          // first channel is black so substract one
          int chanNo = z - 1;
          if (chanNo < 3) {
            const float* in = row[z];
            for (int x = fx ; x < fr ; x++){
                img_in.atXY(x, ry, 0, chanNo) = in[x];
            }
          }
        }      
      }
    


      // polynomial optics setup
      int degree = 3;
      int filter_size = 1; // this doesn't get used.. why is it in source?

      std::cout << "Pupil radius: "<< pupilRadius << std::endl;

      // Sensor scaling
      const float sensor_width = 36;
      const float sensor_scaling = width / sensor_width;
      std::cout << "Sensor scaling: "<< sensor_scaling << std::endl;

      // set min and max wavelengths
      const float lambda_from = 440;
      const float lambda_to = 660;

      // the output is stored here
      img_out = new CImg<float>(width, height, 1, 3, 0);


      //tmp
      int frame = 26;
      float r_pupil = pupilRadius;
      if (frame < 100) r_pupil = 0.1*sqrt(1.f+frame) * pupilRadius;
      cout << "r_pupil radius on frame 26: "<<r_pupil<<endl;

       
      // Focus on 550nm
      Transform4f system = get_system(550, focusDistance, degree);

      // Determine back focal length from degree-1 terms (matrix optics)
      float d3 = find_focus_X(system);
      std::cout << "Focus: " << d3 << std::endl;

      // Compute magnification and output equation system
      float magnification = get_magnification_X(system >> propagate_5(d3));
      std::cout << "Magnification: " << magnification << std::endl;

      // Add that propagation
      //Transform4f prop = propagate_5(d3, degree);
      Transform4f prop = propagate_5(d3 - ((frame>=100)?(0.02*(frame-100)):0), degree);
      system = system >> prop;


    
      // Precompute spectrum
      float *rgb = new float[3 * num_lambdas];
      //float *rgb = new float[12];
      for (int ll = 0; ll < num_lambdas; ++ll) {
        float lambda = lambda_from + (lambda_to - lambda_from) * (ll / (float)(num_lambdas-1));
        if (num_lambdas == 1){
          lambda = 550;
        }

        // Convert wavelength to spectral power
        spectrum_p_to_rgb(lambda, 1, rgb + 3 * ll);
      }



      // Sample optical system at two spectral locations
      Transform4d system_spectral_center = get_system(500, focusDistance) >> prop;
      Transform4d system_spectral_right = get_system(600, focusDistance, degree) >> prop;

      // Obtain (xyworld + xyaperture + lambda) -> (ray) mapping including chromatic effects,
      // by linear interpolation of the two sample systems drawn above
      System54f system_spectral = system_spectral_center.lerp_with(system_spectral_right, 550, 600);

      // dx and dy after propagation are really only needed for Lambertian
      // term; hence: combine them to obtain sin^2 = 1 - cosine^2 term in equation 2:
      system_spectral[2] = (system_spectral[2] * system_spectral[2] + system_spectral[3] * system_spectral[3]);
      system_spectral[2] %= 2;
      System53d system_lambert_cos2 = system_spectral.drop_equation(3);



      // Support of an input image pixel in world plane
      float pixel_size = sensor_width / (float)width / magnification;


      for (int ll = 0; ll < num_lambdas; ++ll) {
        float lambda = lambda_from + (lambda_to - lambda_from) * (ll / (float)(num_lambdas-1));
        if (num_lambdas == 1) lambda = 550;
        std::cout << "[" << lambda << "nm]" << flush;

        // Bake lambda dependency
        System43f system_lambda = system_lambert_cos2.bake_input_variable(4, lambda);
        system_lambda %= degree;


// check the multithreading of for loop speed
#pragma omp parallel for       
        for (int j = 0; j < height; j++) {

          if ( aborted() ){
             _lock.unlock();
            return;
          }

          if (!(j%10)) cout << "." << flush;

          const float y_sensor = ((j - height/2)/(float)width) * sensor_width;
          const float y_world = y_sensor / magnification;

          // Bake y dependency
          System33f system_y = system_lambda.bake_input_variable(1, y_world);

          for (int i = 0; i < width; i++) { 
            const float x_sensor = (i / (float)width - 0.5) * sensor_width;
            const float x_world = x_sensor / magnification;
          
            // Sample intensity at wavelength lambda from source image
            const float rgbin[3] = {
              img_in.linear_atXY(i, j, 0, 0, 0),
              img_in.linear_atXY(i, j, 0, 1, 0),
              img_in.linear_atXY(i, j, 0, 2, 0)};
            float L_in = spectrum_rgb_to_p(lambda, rgbin);

            // Quasi-importance sampling: 
            // pick number of samples according to pixel intensity
            int num_samples = max(1,(int)(L_in * sample_mul));
            float sample_weight = L_in / num_samples;
          
            // With that, we can now start sampling the aperture:
            for (int sample = 0; sample < num_samples; ++sample) {
              // Rejection-sample points from lens aperture:
              float x_ap, y_ap;
              do {
                x_ap = (rand() / (float)RAND_MAX - 0.5) * 2 * r_pupil;
                y_ap = (rand() / (float)RAND_MAX - 0.5) * 2 * r_pupil;
              } while (x_ap * x_ap + y_ap * y_ap > r_pupil * r_pupil);
            
              float in[5], out[4];

              // Fill in variables and evaluate systems:
              in[0] = x_world + pixel_size * (rand()/(float)RAND_MAX - 0.5);
              in[1] = x_ap;
              in[2] = y_ap;

              system_y.evaluate(in,out); 

              // Scale to pixel size:
              out[0] = out[0] * sensor_scaling + width/2;
              out[1] = out[1] * sensor_scaling + height/2;

              // out[2] contains one minus square of Lambertian cosine
              float lambert = sqrt(1 - out[2]);
              if (lambert != lambert){
                lambert = 0; // NaN check
              }

              img_out->set_linear_atXY(lambert * sample_weight * rgb[0 + 3 * ll], out[0], out[1], 0, 0, true);
              img_out->set_linear_atXY(lambert * sample_weight * rgb[1 + 3 * ll], out[0], out[1], 0, 1, true);
              img_out->set_linear_atXY(lambert * sample_weight * rgb[2 + 3 * ll], out[0], out[1], 0, 2, true);
            }
          }
        }
      }


      // Fix gamut problem (pure wavelengths sometimes result in negative RGB)
      for (int j = 0; j < height; ++j){
        for (int i = 0; i < width; ++i){ 
          float max_value =  max(img_out->atXY(i, j, 0, 0), max(img_out->atXY(i, j, 0, 1), img_out->atXY(i, j, 0, 2)));
          img_out->atXY(i,j,0,0) = max(img_out->atXY(i,j,0,0), 0.02f*max_value);
          img_out->atXY(i,j,0,1) = max(img_out->atXY(i,j,0,1), 0.02f*max_value);
          img_out->atXY(i,j,0,2) = max(img_out->atXY(i,j,0,2), 0.02f*max_value);
        } 
      }


      _firstTime = false;
      std::cout << " done " << std::endl;
    }
  } // end lock



  // Copy values from CImg framebuffer to nuke pixels
  Row in(l, r);
  in.get(input0(), y, l, r, channels);
  
  if (aborted()){
    return;
  }


  foreach(z, channels) {
    float *CUR = row.writable(z) + l;
    const float* inptr = in[z] + l;
    const float *END = row[z] + r;
    int x = l;

    while ( CUR < END ) {
      int chanNo = z - 1;
      
      if (chanNo < 3) { 
        *CUR++ = img_out->atXY(x++, y, 0, chanNo);
      } else {
        *CUR++ = *inptr++;
      }
    }
  }

}




// Lens system setup
Transform4f polynomialOpticsBlur::get_system(float lambda, float d0, int degree) {
 // Let's simulate Edmund Optics achromat #NT32-921:
  /* Clear Aperture CA (mm)   39.00
     Eff. Focal Length EFL (mm)   120.00
     Back Focal Length BFL (mm)   111.00
     Center Thickness CT 1 (mm)   9.60
     Center Thickness CT 2 (mm)   4.20
     Radius R1 (mm)   65.22
     Radius R2 (mm)   -62.03
     Radius R3 (mm)   -1240.67
     Substrate  N-SSK8/N-SF10 
  */

  OpticalMaterial glass1("N-SSK8", true);
  OpticalMaterial glass2("N-SF10", true);
  
  const float R1 = 65.22;
  const float d1 = 9.60;
  const float R2 = -62.03;
  const float d2 = 4.20;
  const float R3 = -1240.67;

  return two_plane_5(d0, degree)
    >> refract_spherical_5(R1,1.f,glass1.get_index(lambda), degree)
    >> propagate_5(d1, degree)
    >> refract_spherical_5(R2,glass1.get_index(lambda),glass2.get_index(lambda), degree)
    >> propagate_5(d2, degree)
    >> refract_spherical_5(R3,glass2.get_index(lambda), 1.f, degree);
}