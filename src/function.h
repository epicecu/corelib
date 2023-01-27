#ifndef FUNCTION_H
#define FUNCTION_H

/**
 * Function
 * @brief Funciton base class
 */
class Function{
public:
    /// Called during setup 
    void initilise() {
        this->performInitilise();
    }

    /// Called by the main loop
    void iterate() {
        this->performIterate();
    }


protected:
    /// Called during setup
    virtual void performInitilise() = 0;

    /// Called every x Hz selected by setting the variable
    virtual void performIterate() = 0;

};

#endif // FUNCTION_H
