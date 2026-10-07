#include <assert.h>
#include <math.h>
#include "ValidateMatrix2D.h"


template<class T>
std::vector<ValidationErrorInfo<T> >
Validate<T>::operator()( const Matrix2D<T>& s, const Matrix2D<T>& t )
{
    std::vector<ValidationErrorInfo<T> > ret;
    // Absolute tolerance applies only when the expected value is zero.
    const double absErrThreshold = 1.0e-6;

    // ensure matrices are same shape
    assert( (s.GetNumRows() == t.GetNumRows()) && (s.GetNumColumns() == t.GetNumColumns()) );

    for( unsigned int i = 0; i < s.GetNumRows(); i++ )
    {
        for( unsigned int j = 0; j < s.GetNumColumns(); j++ )
        {
            T expVal = s.GetConstData()[i][j];
            T actualVal = t.GetConstData()[i][j];
            T delta = fabs( actualVal - expVal );
            T relError = (expVal != 0.0f) ? delta / fabs(expVal) : delta;
            double errorThreshold = (expVal != 0.0f) ? relErrThreshold : absErrThreshold;

            if( !isfinite(expVal) || !isfinite(actualVal) ||
                relError > errorThreshold )
            {
                ret.push_back( ValidationErrorInfo<T>( i, j, actualVal, expVal, relError ) );
            }
        }
    }

    return ret;
}


