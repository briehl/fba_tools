////////////////////////////////////////////////////////////////////////////////
//    MFAToolkit: Software for running flux balance analysis on stoichiometric models
//    Software developer: Christopher Henry (chenry@mcs.anl.gov), MCS Division, Argonne National Laboratory
//    Copyright (C) 2007  Argonne National Laboratory/University of Chicago. All Rights Reserved.
//
//    This program is free software: you can redistribute it and/or modify
//    it under the terms of the GNU General Public License as published by
//    the Free Software Foundation, either version 3 of the License, or
//    (at your option) any later version.
//
//    This program is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU General Public License for more details.
//
//    You should have received a copy of the GNU General Public License
//    along with this program.  If not, see <http://www.gnu.org/licenses/>.
//    For more information on MFAToolkit, see <http://bionet.mcs.anl.gov/index.php/Model_analysis>.
////////////////////////////////////////////////////////////////////////////////

#include "MFAToolkit.h"

int lpcount = 0;
vector<string>* MFALog;

MFAProblem::MFAProblem() {
  MFALog = new vector<string>;
	SetParameter("MFA output path",(FOutputFilepath()+GetParameter("MFA output")).data());
	MinFluxConstraint = NULL;
	ObjectiveConstraint = NULL;
	SourceDatabase = NULL;
	SetSolver(GetParameter("MFASolver").data());
	Max = true;
	ProbType = LINEAR;
	NumLocalSolve = 1;
	ObjFunct = NULL;
	RelaxIntegerVariables = false;
	LoadedRelaxation = false;
	UseTightBounds = false;
	ProblemLoaded = false;

	string Filename(FOutputFilepath());
	Filename.append(GetParameter("MFA problem report filename"));
	ProblemIndex = GetNumberOfLinesInFile(Filename);
	if (ProblemIndex > 0) {
		ProblemIndex--;
	}

	MFAProblemClockIndex = StartClock(-1);
}

MFAProblem::~MFAProblem() {
  delete MFALog;
	for (int i=0; i < FNumVariables(); i++) {
		delete Variables[i];
	}

	for (int i=0; i < FNumConstraints(); i++) {
		delete Constraints[i];
	}

	for (int i=0; i < FNumSolutions(); i++) {
		delete Solutions[i];
	}

	if (ObjFunct != NULL) {
		delete ObjFunct;
	}

	ClearClock(MFAProblemClockIndex);
}

//Input functions
void MFAProblem::SetMax() {
	Max = true;
}

void MFAProblem::SetMin() {
	Max = false;
}

void MFAProblem::SetSolver(const char* InSolver) {
	string Temp = InSolver;
	if (Temp.compare("CPLEX") == 0) {
		Solver = CPLEX;
	} else if (Temp.compare("GLPK") == 0) {
		Solver = GLPK;
	}  else if (Temp.compare("SCIP") == 0) {
		Solver = SOLVER_SCIP;
	} else {
		Solver = LINDO;
	}
}

void MFAProblem::SetNumLocalSolve(int InNumLocalSolve) {
	NumLocalSolve = InNumLocalSolve;
}

void MFAProblem::ResetIndecies() {
	for (int i=0; i < FNumVariables(); i++) {
		Variables[i]->Index = i;
	}

	for (int i=0; i < FNumConstraints(); i++) {
		Constraints[i]->Index = i;
	}
}

void MFAProblem::ClearSolutions(int Start, int End, bool DeleteThem) {
	if (End == -1) {
		End = FNumSolutions()-1;
	}
	if (Start == -1) {
		Start = 0;
	}
	
	if (DeleteThem) {
		for (int i=Start; i <= End; i++) {
			delete Solutions[i];
		}
	}

	if (Start == 0 && End == (FNumSolutions()-1)) {
		Solutions.clear();
	} else {
		if (Solutions.size() > 0) {
			Solutions.erase(Solutions.begin()+Start,Solutions.begin()+End+1);
		}
	}
}

int MFAProblem::AddVariable(MFAVariable* InVariable) {
	Variables.push_back(InVariable);
	InVariable->Index = (FNumVariables()-1);
	return (FNumVariables()-1);
}

int MFAProblem::AddConstraint(LinEquation* InConstraint) {
	if (InConstraint != NULL) {
		Constraints.push_back(InConstraint);
		InConstraint->Index = (FNumConstraints()-1);
	}
	return (FNumConstraints()-1);
}

void MFAProblem::AddObjective(LinEquation* InObjective) {
	ClearObjective();
	ObjFunct = InObjective;
}

void MFAProblem::AddOptimizeVariableObjective(MFAVariable* InVariable, bool InMax) {
	LinEquation* NewObjective = InitializeLinEquation();
	NewObjective->Coefficient.push_back(1);
	NewObjective->Variables.push_back(InVariable);
	AddObjective(NewObjective);
	if (InMax) {
		SetMax();
	} else {
		SetMin();
	}
}

void MFAProblem::ClearObjective(bool DeleteThem) {
	if (DeleteThem && ObjFunct != NULL) {
		delete ObjFunct;
	}

	ObjFunct = NULL;
}

void MFAProblem::ClearConstraints(bool DeleteThem) {
	if (DeleteThem) {
		for (int i=0; i < FNumConstraints(); i++) {
			delete Constraints[i];
		}
	}

	Constraints.clear();
}

void MFAProblem::ClearVariables(bool DeleteThem) {
	if (DeleteThem) {
		for (int i=0; i < FNumVariables(); i++) {
			if (Variables[i]->AssociatedSpecies != NULL) {
				Variables[i]->AssociatedSpecies->ClearMFAVariables(false);
			}
			if (Variables[i]->AssociatedReaction != NULL) {
				Variables[i]->AssociatedReaction->ClearMFAVariables(false);
			}
			if (Variables[i]->AssociatedGene != NULL) {
				Variables[i]->AssociatedGene->ClearMFAVariables(false);
			}
			if (Variables[i]->AssociatedInterval != NULL) {
				Variables[i]->AssociatedInterval->ClearMFAVariables(false);
			}
			delete Variables[i];
		}
	}

	Variables.clear();
}

void MFAProblem::DetermineProbType() {
	bool Integer = false;
	bool Nonlinear = false;
	bool Quadratic = false;

	for (int i=0; i < FNumVariables(); i++) {
		if (Variables[i]->Binary || Variables[i]->Integer) {
			if (!RelaxIntegerVariables && GetParameter("Always relax integer variables").compare("1") != 0) {
				Integer = true;
			}
		}
	}
	for (int i=0; i < FNumConstraints(); i++) {
		if (Constraints[i]->ConstraintType == NONLINEAR) {
			Nonlinear = true;
		} else if (Constraints[i]->ConstraintType == QUADRATIC) {
			Quadratic = true;
		}
	}
	if (GetObjective()->ConstraintType == NONLINEAR) {
		Nonlinear = true;
	} else if (GetObjective()->ConstraintType == QUADRATIC) {
		Quadratic = true;
	}
	if (Integer) {
		if (Nonlinear) {
			ProbType = MINP;
		} else if (Quadratic) {
			ProbType = MIQP;
		} else {
			ProbType = MILP;
		}
	} else {
		if (Nonlinear) {
			ProbType = NP;
		} else if (Quadratic) {
			ProbType = QP;
		} else {
			ProbType = LP;
		}
	}
	Solver = SelectSolver(ProbType,Solver);
}

void MFAProblem::AddSumObjective(int VarType, bool Quadratic, bool Append, double Coeff, bool ForeignOnly) {
	if (!Append) {
		ClearObjective(true);
		ObjFunct = InitializeLinEquation();
	}
	
	for (int i=0; i < FNumVariables(); i++) {
		if (GetVariable(i)->Type == VarType) {
			if (!ForeignOnly || (GetVariable(i)->AssociatedReaction != NULL && GetVariable(i)->AssociatedReaction->GetData("FOREIGN",STRING).length() > 0) || (GetVariable(i)->AssociatedSpecies != NULL && GetVariable(i)->AssociatedSpecies->GetData("FOREIGN",STRING).length() > 0)) {
				if (Quadratic) {
					ObjFunct->QuadOne.push_back(GetVariable(i));
					ObjFunct->QuadTwo.push_back(GetVariable(i));
					ObjFunct->QuadCoeff.push_back(Coeff);
					ObjFunct->ConstraintType = QUADRATIC;
				} else {
					if (VarType == INTERVAL_USE) {
						ObjFunct->Coefficient.push_back(Coeff*GetVariable(i)->AssociatedInterval->FNumTotalGenes());
					} else {
						ObjFunct->Coefficient.push_back(Coeff);
					}
					ObjFunct->Variables.push_back(GetVariable(i));
					ObjFunct->ConstraintType = LINEAR;
				}
			}
		}
	}

	ObjFunct->ConstraintMeaning.assign(ConvertVariableType(VarType));
	ObjFunct->ConstraintMeaning.assign(" sum objective");
}	

void MFAProblem::AddMassBalanceConstraints(Data* InData) {
	map<Species* , vector<LinEquation*> , std::less<Species*> > MassBalanceConstraints; 
	for (int i=0; i < InData->FNumReactions(); i++) {
		for (int j=0; j < InData->GetReaction(i)->FNumReactants(); j++) {
			//First I check to see if the constraint already exists
			vector<LinEquation*>& SpeciesConstraints = MassBalanceConstraints[InData->GetReaction(i)->GetReactant(j)];
			int ReactantCompartment = InData->GetReaction(i)->GetReactantCompartment(j);
			LinEquation* NewConstraint = NULL;
			string SearchString(GetCompartment(ReactantCompartment)->Abbreviation);
			SearchString.append("_mass_balance");
			for (int k=0; k < int(SpeciesConstraints.size()); k++) {
				if (SpeciesConstraints[k]->ConstraintMeaning.compare(SearchString) == 0) {
					NewConstraint = SpeciesConstraints[k];
				}
			}	
			//If the constraint does not exist, I create it
			if (NewConstraint == NULL) {
				NewConstraint = InitializeLinEquation();
				NewConstraint->ConstraintMeaning.assign(GetCompartment(ReactantCompartment)->Abbreviation);
				NewConstraint->ConstraintMeaning.append("_mass_balance");
				NewConstraint->ConstraintType = LINEAR;
				NewConstraint->RightHandSide = 0;
				NewConstraint->EqualityType = EQUAL;
				NewConstraint->AssociatedSpecies = InData->GetReaction(i)->GetReactant(j);
				if (InData->GetReaction(i)->GetReactant(j)->GetMFAVar(DRAIN_FLUX,ReactantCompartment) != NULL) {
					NewConstraint->Coefficient.push_back(1);
					NewConstraint->Variables.push_back(InData->GetReaction(i)->GetReactant(j)->GetMFAVar(DRAIN_FLUX,ReactantCompartment));
				} else {
					if (InData->GetReaction(i)->GetReactant(j)->GetMFAVar(FORWARD_DRAIN_FLUX,ReactantCompartment) != NULL) {
						NewConstraint->Coefficient.push_back(1);
						NewConstraint->Variables.push_back(InData->GetReaction(i)->GetReactant(j)->GetMFAVar(FORWARD_DRAIN_FLUX,ReactantCompartment));
					}
					if (InData->GetReaction(i)->GetReactant(j)->GetMFAVar(REVERSE_DRAIN_FLUX,ReactantCompartment) != NULL) {
						NewConstraint->Coefficient.push_back(-1);
						NewConstraint->Variables.push_back(InData->GetReaction(i)->GetReactant(j)->GetMFAVar(REVERSE_DRAIN_FLUX,ReactantCompartment));
					}
				}
				SpeciesConstraints.push_back(NewConstraint);
				AddConstraint(NewConstraint);
			}
			if (InData->GetReaction(i)->GetMFAVar(FLUX) != NULL) {
				NewConstraint->Variables.push_back(InData->GetReaction(i)->GetMFAVar(FLUX));
				NewConstraint->Coefficient.push_back(InData->GetReaction(i)->GetReactantCoef(j));
			} else {
				if (InData->GetReaction(i)->GetMFAVar(FORWARD_FLUX) != NULL) {
					NewConstraint->Variables.push_back(InData->GetReaction(i)->GetMFAVar(FORWARD_FLUX));
					NewConstraint->Coefficient.push_back(InData->GetReaction(i)->GetReactantCoef(j));
				}
				if (InData->GetReaction(i)->GetMFAVar(REVERSE_FLUX) != NULL) {
					NewConstraint->Variables.push_back(InData->GetReaction(i)->GetMFAVar(REVERSE_FLUX));
					NewConstraint->Coefficient.push_back(-InData->GetReaction(i)->GetReactantCoef(j));
				}
			}
		}
	}
}

LinEquation* MFAProblem::AddSumConstraint(int VarType, bool Quadratic, double Coeff, double RHS, int EqualityType) {
	LinEquation* NewConstraint = InitializeLinEquation();

	for (int i=0; i < FNumVariables(); i++) {
		if (GetVariable(i)->Type == VarType) {
			if (Quadratic) {
				ObjFunct->QuadOne.push_back(GetVariable(i));
				ObjFunct->QuadTwo.push_back(GetVariable(i));
				ObjFunct->QuadCoeff.push_back(Coeff);
				ObjFunct->ConstraintType = QUADRATIC;
			} else {
				NewConstraint->Variables.push_back(GetVariable(i));
				NewConstraint->Coefficient.push_back(Coeff);
				NewConstraint->ConstraintType = LINEAR;
			}
		}
	}

	NewConstraint->RightHandSide = RHS;
	NewConstraint->EqualityType = EqualityType;
	NewConstraint->Index = FNumConstraints();
	AddConstraint(NewConstraint);
	NewConstraint->ConstraintMeaning.assign(ConvertVariableType(VarType));
	NewConstraint->ConstraintMeaning.assign(" sum constraint");

	return NewConstraint;
}

LinEquation* MFAProblem::AddUseSolutionConst(OptSolutionData* SolutionData, vector<int> VariableTypes, OptimizationParameter* InParameters) {
	//If we are minimizing the addition of foreign reactions, then only foreign reaction use variables should be manipulated
	bool ForeignOnly = false;
	if (InParameters->LoadForeignDB && InParameters->MinimizeForeignReactions) {
		ForeignOnly = true;
	}

	LinEquation* NewConstraint = InitializeLinEquation();
	int Count = 0;
	if (VariableTypes[0] == OBJECTIVE_TERMS) {
		for (int i=0; i < int(ObjFunct->Variables.size()); i++) {
			bool IncludeVariable = true;
			if (VariableTypes.size() > 1) {
				IncludeVariable = false;
				for (int j=1; j < int(VariableTypes.size()); j++) {
					if (VariableTypes[j] == ObjFunct->Variables[i]->Type) {
						IncludeVariable = true;
						break;
					}
				}
			}
			if (IncludeVariable) {
				if (!InParameters->GapGeneration) {
					if (SolutionData->SolutionData[ObjFunct->Variables[i]->Index] > 0.5) {
						Count++;
						NewConstraint->Variables.push_back(ObjFunct->Variables[i]);
						NewConstraint->Coefficient.push_back(1);
					}
				} else {
					if (SolutionData->SolutionData[ObjFunct->Variables[i]->Index]<0.5) {
						Count++;
						NewConstraint->Variables.push_back(ObjFunct->Variables[i]);
						NewConstraint->Coefficient.push_back(1);
					}
				}
			}
		}
	} else {
		for (int i=0; i < FNumVariables(); i++) {
			for (int j=0; j < int(VariableTypes.size()); j++) {
				if (!ForeignOnly || (GetVariable(i)->AssociatedReaction != NULL && GetVariable(i)->AssociatedReaction->GetData("FOREIGN",STRING).length() > 0) || (GetVariable(i)->AssociatedSpecies != NULL && GetVariable(i)->AssociatedSpecies->GetData("FOREIGN",STRING).length() > 0)) {
					if (GetVariable(i)->Type == VariableTypes[j]) {
						if (!InParameters->GapGeneration) {
							if (SolutionData->SolutionData[i]>0.5) {
								Count++;
								NewConstraint->Variables.push_back(GetVariable(i));
								NewConstraint->Coefficient.push_back(1);
							}
						} else {
							if (SolutionData->SolutionData[i]<0.5) {
								Count++;
								NewConstraint->Variables.push_back(GetVariable(i));
								NewConstraint->Coefficient.push_back(1);
							}
						}
						j = int(VariableTypes.size()); 
					}
				}
			}
		}
	}
	
	NewConstraint->RightHandSide = Count-1;
	if (Count == 1 && InParameters->GapGeneration) {
		NewConstraint->RightHandSide = 1;
	}
	if (!InParameters->GapGeneration) {
		NewConstraint->EqualityType = LESS;
	} else {
		NewConstraint->EqualityType = GREATER;
	}
	AddConstraint(NewConstraint);
	NewConstraint->ConstraintType = LINEAR;
	NewConstraint->ConstraintMeaning.assign("MILP solution constraint");

	return NewConstraint;
}

void MFAProblem::EnforceIntergerSolution(OptSolutionData* SolutionData, vector<int> VariableTypes, bool ForeignOnly, bool RefreshSolver) {
	int Count = 0;
	if (VariableTypes[0] == OBJECTIVE_TERMS) {
		for (int i=0; i < int(ObjFunct->Variables.size()); i++) {
			if (SolutionData->SolutionData[ObjFunct->Variables[i]->Index]>0.5) {
				ObjFunct->Variables[i]->UpperBound = 1;
				ObjFunct->Variables[i]->LowerBound = 1;
			} else {
				ObjFunct->Variables[i]->UpperBound = 0;
				ObjFunct->Variables[i]->LowerBound = 0;
			}
			if (RefreshSolver) {
				LoadVariable(ObjFunct->Variables[i]->Index);
			}
		}
		return;
	}
	
	for (int i=0; i < FNumVariables(); i++) {
		for (int j=0; j < int(VariableTypes.size()); j++) {
			if (!ForeignOnly || (GetVariable(i)->AssociatedReaction != NULL && GetVariable(i)->AssociatedReaction->GetData("FOREIGN",STRING).length() > 0) || (GetVariable(i)->AssociatedSpecies != NULL && GetVariable(i)->AssociatedSpecies->GetData("FOREIGN",STRING).length() > 0)) {
				if (GetVariable(i)->Type == VariableTypes[j]) {
					if (SolutionData->SolutionData[i]>0.5) {
						GetVariable(i)->UpperBound = 1;
						GetVariable(i)->LowerBound = 1;
					} else {
						GetVariable(i)->UpperBound = 0;
						GetVariable(i)->LowerBound = 0;
					}
					if (RefreshSolver) {
						LoadVariable(i);
					}
					j = int(VariableTypes.size()); 
				}
			}
		}
	}
}

void MFAProblem::RelaxSolutionEnforcement(vector<int> VariableTypes, bool ForeignOnly, bool RefreshSolver) {
	int Count = 0;
	for (int i=0; i < FNumVariables(); i++) {
		for (int j=0; j < int(VariableTypes.size()); j++) {
			if (!ForeignOnly || (GetVariable(i)->AssociatedReaction != NULL && GetVariable(i)->AssociatedReaction->GetData("FOREIGN",STRING).length() > 0) || (GetVariable(i)->AssociatedSpecies != NULL && GetVariable(i)->AssociatedSpecies->GetData("FOREIGN",STRING).length() > 0)) {
				if (GetVariable(i)->Type == VariableTypes[j]) {
					GetVariable(i)->UpperBound = 1;
					GetVariable(i)->LowerBound = 0;
					j = int(VariableTypes.size()); 
				}
				if (RefreshSolver) {
					LoadVariable(i);
				}
			}
		}
	}
}

LinEquation* MFAProblem::MakeObjectiveConstraint(double Value, int Equality) {
	LinEquation* NewConstraint = CloneLinEquation(ObjFunct);
	
	NewConstraint->RightHandSide = Value;
	NewConstraint->EqualityType = Equality;
	NewConstraint->ConstraintMeaning.assign("objective constraint");
	AddConstraint(NewConstraint);
	
	return NewConstraint;
}

int MFAProblem::BuildMFAProblem(Data* InData,OptimizationParameter*& InParameters) {
	SourceDatabase = InData;
	if (InData == NULL) {
		FErrorFile() << "Failed to build MFA problem: no data was passed in" << endl;
		FlushErrorFile();
		return FAIL;
	}
	
	if (InParameters == NULL) {
		InParameters = ReadParameters();
		if (InParameters == NULL) {
			FErrorFile() << "Failed to build MFA problem: could not read in parameter" << endl;
			FlushErrorFile();
			return FAIL;
		}
	}

	if (InParameters->IncludeDeadEnds && InParameters->DetermineMinimalMedia) {
		InData->FindDeadEnds();
		for (int i=0; i < InData->FNumSpecies(); i++) {
			if (InData->GetSpecies(i)->GetData("DEAD",STRING).compare("yes") == 0) {
				InParameters->ExchangeSpecies.push_back(InData->GetSpecies(i)->GetData("DATABASE",STRING));
				InParameters->ExchangeComp.push_back(GetCompartment("c")->Index);
				InParameters->ExchangeMax.push_back(10000);
				InParameters->ExchangeMin.push_back(-10000);
			}
		}
	}

	InData->ResetAllBools(false,true,false,true,true,true,true);

	//I read in foreign database reactions and compounds
	if (InParameters->LoadForeignDB && InData->GetData("FOREIGNDB",STRING).length() == 0) {
		int FinalNonForeignRxn = InData->FNumReactions();
		int FinalNonForeignCpd = InData->FNumSpecies();
		string Filename = GetParameter("Filename for foreign reaction database");		
		string OriginalName = InData->GetData("NAME",STRING);
		InData->ClearData("NAME",STRING);
		InData->AddData("FOREIGNDB",RemoveExtension(RemovePath(Filename)).data(),STRING);
		InData->AddData("NAME",RemoveExtension(RemovePath(Filename)).data(),STRING);
		cout<<"MFA-Loading System :"<<Filename<<endl;
		InData->LoadSystem(Filename);
		Filename = RemoveExtension(Filename);
		for (int i=FinalNonForeignCpd; i < InData->FNumSpecies(); i++) {
			InData->GetSpecies(i)->AddData("FOREIGN",OriginalName.data(),STRING);
			vector<string> Links = InData->GetSpecies(i)->GetAllData("LINK",STRING);
			for (int j=0; j < int(Links.size()); j++) {
				if (OriginalName.compare(Links[j]) == 0) {
					InData->GetSpecies(i)->SetMark(true);
					j = int(Links.size());
				}
			}
		}
		for (int i=FinalNonForeignRxn; i < InData->FNumReactions(); i++) {
			InData->GetReaction(i)->AddData("FOREIGN",OriginalName.data(),STRING);
			if (InData->GetReaction(i)->ContainsMarkedReactants()) {
				InData->GetReaction(i)->ReplaceAllLinkedReactants();
				InData->GetReaction(i)->BalanceReaction(true,false);
			}
		}
		InData->RemoveMarkedSpecies(true);
		InData->RemoveMarkedReactions(true);
		InData->ReindexSpecies();
		InData->ReindexReactions();
	}
	
	//Reading in and marking lumped reactions
	if (InParameters->AddLumpedReactions && InParameters->ThermoConstraints) {
		int FinalNonlumped = InData->FNumReactions();
		cout<<"MFA-Loading Lumped System :"<<GetParameter("lumped reaction database")<<endl;
		InData->LoadSystem(GetParameter("lumped reaction database"));
		for (int i=FinalNonlumped; i < InData->FNumReactions(); i++) {
			InData->GetReaction(i)->SetMark(true);
		}
	}
	
	//Adding all variables except the use variables
	for (int i=0; i < InData->FNumReactions(); i++) {
		InData->GetReaction(i)->CreateMFAVariables(InParameters);
	}
	for (int i=0; i < InData->FNumSpecies(); i++) {
		InData->GetSpecies(i)->CreateMFAVariables(InParameters);
	}
	//Now I adjust all bounds based on user input and tight bounds read in
	if (InParameters->LoadTightBounds) {
		LoadTightBounds(InData,true);
	}
	if (GetParameter("Base compound regulation on media files").compare("1") == 0) {
		for (int i=0; i < InData->FNumSpecies(); i++) {
			InData->GetSpecies(i)->AddUseVariables(InParameters);
			InData->GetSpecies(i)->GetAllMFAVariables(Variables);
		}
	}
	ApplyInputBounds(InParameters->UserBounds,InData);
	//Adding drain fluxes for reactions 
	if (GetParameter("Add reaction drain fluxes").compare("none") != 0) {
		vector<string>* strings = StringToStrings(GetParameter("Add reaction drain fluxes"),",;");
		for (int i=0; i < int(strings->size()); i++) {
			Reaction* tempRxn = InData->FindReaction("DATABASE",(*strings)[i].data());
			if (tempRxn != NULL) {
				tempRxn->CreateReactionDrainFluxes();
			}
		}
		delete strings;
	}
	//Enforcing the specified KO reactions
	for (int i=0; i < int(InParameters->KOReactions.size()); i++) {
		Reaction* Temp = InData->FindReaction("DATABASE",InParameters->KOReactions[i].data());
		if (Temp != NULL) {
			if (Temp->GetData("FOREIGN",STRING).compare("Reaction") == 0 || GetParameter("Perform gap filling").compare("0") == 0) {
				Temp->UpdateBounds(FLUX,0,0);
			}
		}
	}
	for (int i=0; i < InData->FNumGenes(); i++) {
		InData->GetGene(i)->SetMark(false);
	}
	for (int i=0; i < int(InParameters->KOGenes.size()); i++) {
		Gene* Temp = InData->FindGene("DATABASE",InParameters->KOGenes[i].data());
		if (Temp != NULL) {
			Temp->SetMark(true);
		}
	}
	for (int j=0; j < InData->FNumReactions(); j++) {
		if (InData->GetReaction(j)->CheckForKO()) {
			InData->GetReaction(j)->UpdateBounds(FLUX,0,0);
		}
	}
	for (int i=0; i < InData->FNumReactions(); i++) {
		InData->GetReaction(i)->AddUseVariables(InParameters);
		InData->GetReaction(i)->GetAllMFAVariables(Variables);
	}
	if (GetParameter("Base compound regulation on media files").compare("0") == 0) {
		for (int i=0; i < InData->FNumSpecies(); i++) {
			InData->GetSpecies(i)->AddUseVariables(InParameters);
			InData->GetSpecies(i)->GetAllMFAVariables(Variables);
		}
	}
	
	//Now that all of the necessary variables exist, I can read in and add the special constraints
	ApplyInputConstraints(InParameters->AddConstraints,InData);

	LinEquation* NewConstraint = NULL;
	//Now I add mass balance constraints
	if (InParameters->MassBalanceConstraints) {
		AddMassBalanceConstraints(InData);
	}
	
	//Now I add thermo constraints
	if (InParameters->ThermoConstraints) {
		//Adding group energy variables for any structural cue with an unknown energy
		if (!InParameters->SimpleThermoConstraints) {
			for (int i=0; i < InData->FNumStructuralCues(); i++) {
				if (InData->GetStructuralCue(i)->FEstDeltaG() == -10000) {
					AddVariable(InData->GetStructuralCue(i)->CreateMFAVariable(DELTAGG_ENERGY,-1,-1000,1000));
				}
			}
		}
		for (int i=0; i < InData->FNumReactions(); i++) {
			if (InData->GetReaction(i)->FNumReactants() != 2 || InData->GetReaction(i)->GetReactant(0)->FFormula().compare("H2O") != 0 || InData->GetReaction(i)->GetReactant(1)->FFormula().compare("H2O") != 0) {
				//Adding the reaction delta G error constraint if error is to be accounted for
				if (InData->GetReaction(i)->FEstDeltaG() != FLAG && InParameters->DeltaGError && !InParameters->SimpleThermoConstraints) {
					AddConstraint(CreateReactionErrorConstraint(InData->GetReaction(i),InParameters));
				}
				//I add the gibbs free energy equation constraint for all reactions whether they are lumped or not
				if (!InData->GetReaction(i)->IsBiomassReaction()) {
					AddConstraint(CreateGibbsEnergyConstraint(InData->GetReaction(i),InParameters));
				}
			}
		}
		if (!InParameters->SimpleThermoConstraints) {
			for (int i=0; i < InData->FNumSpecies(); i++) {
				if (InData->GetSpecies(i)->FFormula().compare("H") != 0 && InData->GetSpecies(i)->FFormula().compare("H2O") != 0 && InData->GetSpecies(i)->FNumNoIDGroups() == 0 && InData->GetSpecies(i)->FNumStructuralCues() > 0) {
					string cpd_ID = InData->GetSpecies(i)->GetData("DATABASE",STRING).data();
					if (InParameters->PotentialEnergyCompoundsInclusive && InParameters->PotentialEnergyCompounds.count(cpd_ID) > 0) {
						CreateSpeciesGibbsEnergyConstraint(InData->GetSpecies(i),InParameters);
					} else if (!InParameters->PotentialEnergyCompoundsInclusive && InParameters->PotentialEnergyCompounds.count(cpd_ID) == 0) {
						CreateSpeciesGibbsEnergyConstraint(InData->GetSpecies(i),InParameters);
					}
				}
			}
		}
	}

	//Now I add the use variable constraints
	if (InParameters->ReactionsUse || InParameters->DrainUseVar) {
		for	(int i=0; i < FNumVariables(); i++) {
			if (GetVariable(i)->Binary) {
				LinEquation* UseVarConstraint = CreateUseVariableConstraint(GetVariable(i),InParameters);
				if (UseVarConstraint != NULL) {
					AddConstraint(UseVarConstraint);
					if (GetParameter("Add positive use variable constraints").compare("1") == 0) {
						UseVarConstraint = CreateUseVariablePositiveConstraint(GetVariable(i),InParameters);
						if (UseVarConstraint != NULL) {
							AddConstraint(UseVarConstraint);
						}
					}
				}
				if (GetParameter("Base compound regulation on media files").compare("0") == 0 && !InParameters->GapGeneration && GetVariable(i)->Type == FORWARD_DRAIN_USE && GetVariable(i)->AssociatedSpecies != NULL && GetVariable(i)->AssociatedSpecies->GetMFAVar(REVERSE_DRAIN_USE,GetVariable(i)->Compartment) != NULL) {
					//There are no thermodynamic constraints on drain fluxes, so I need constraints to ensure that forward and backward reactions cannot take place simultaneously
					LinEquation* NewConstraint = InitializeLinEquation();
					NewConstraint->AssociatedSpecies = GetVariable(i)->AssociatedSpecies;
					NewConstraint->EqualityType = LESS;
					NewConstraint->RightHandSide = 1;
					NewConstraint->ConstraintMeaning.assign("Preventing simultaneous operation of forward and backward reactions");
					NewConstraint->Coefficient.push_back(1);
					NewConstraint->Coefficient.push_back(1);
					NewConstraint->Variables.push_back(GetVariable(i));
					NewConstraint->Variables.push_back(GetVariable(i)->AssociatedSpecies->GetMFAVar(REVERSE_DRAIN_USE,GetVariable(i)->Compartment));
					AddConstraint(NewConstraint);
				} else if (!InParameters->GapGeneration && GetVariable(i)->Type == FORWARD_USE && GetVariable(i)->AssociatedReaction != NULL && GetVariable(i)->AssociatedReaction->GetMFAVar(REVERSE_USE) != NULL && GetVariable(i)->AssociatedReaction->GetMFAVar(DELTAG) == NULL) {
					//I only write this constraint if the delta G does not exist
					LinEquation* NewConstraint = InitializeLinEquation();
					NewConstraint->AssociatedReaction = GetVariable(i)->AssociatedReaction;
					NewConstraint->EqualityType = LESS;
					NewConstraint->RightHandSide = 1;
					NewConstraint->ConstraintMeaning.assign("Preventing simultaneous operation of forward and backward reactions");
					NewConstraint->Coefficient.push_back(1);
					NewConstraint->Coefficient.push_back(1);
					NewConstraint->Variables.push_back(GetVariable(i));
					NewConstraint->Variables.push_back(GetVariable(i)->AssociatedReaction->GetMFAVar(REVERSE_USE));
					AddConstraint(NewConstraint);
				}
			}
		}	
	}
	
	//Adding use variables for genes
	if (InParameters->GeneConstraints) {
		//Adding use variables for all genes with at least one reaction mapped to them
		for (int i=0; i < InData->FNumGenes(); i++) {
			//Gene use variables only need to be added for genes that are mapped to at least one reaction
			if (InData->GetGene(i)->FNumReactions() > 0 || GetParameter("Add regulatory constraint to problem").compare("1") == 0) {
				AddVariable(InData->GetGene(i)->CreateMFAVariable(InParameters));
			}
		}
		//Adding regulatory constraints so TF genes that might be missing from the model are added
		if (GetParameter("Add regulatory constraint to problem").compare("1") == 0) {
			if (AddRegulatoryConstraints(InParameters,InData) != SUCCESS) {
				FErrorFile() << "Failed to build MFA problem: could not generate regulatory constraints!" << endl;
				FlushErrorFile();
				return FAIL;
			}
		}
		//Setting the gene use variable to zero for all knocked out genes
		for (int i=0; i < int(InParameters->KOGenes.size()); i++) {
			Gene* Temp = InData->FindGene("DATABASE;NAME",InParameters->KOGenes[i].data());
			if (Temp != NULL) {
				MFAVariable* geneVar = Temp->GetMFAVar();
				geneVar->UpperBound = 0;
				geneVar->LowerBound = 0;
			}
		}
	}

	//Removing the lumped reactions from the database
	if (InParameters->AddLumpedReactions) {
		InData->RemoveMarkedReactions(false);
	}

	//Adding variables and constraints associated with genes and complexes (COMPLEX_USE and GENE_USE)
	if (InParameters->GeneConstraints) {
		//Complex variables were already created and added by the reactions
		//Now creating the reaction-gene dependency constraints
		for (int i=0; i < InData->FNumReactions(); i++) {
			//Multiple constraints are created for each reaction: a constraint linking flux to genes and complexes, and a constraint linking genes to each complex
			vector<LinEquation*> GeneReactionConstraints = InData->GetReaction(i)->CreateGeneReactionConstraints();
			for (int j=0; j < int(GeneReactionConstraints.size()); j++) {
				for (int k=0; k < int(GeneReactionConstraints[j]->Variables.size());k++) {
					if (GeneReactionConstraints[j]->Variables[k]->Type == COMPLEX_USE && GeneReactionConstraints[j]->Variables[k]->Index == -1) {
						AddVariable(GeneReactionConstraints[j]->Variables[k]);
					}
				}
				AddConstraint(GeneReactionConstraints[j]);
			}
		}

		//Now creating new gene-reaction dependency constraints
		// <If gene is used, then at least one reaction associated with it is used.>
		for (int i=0; i < InData->FNumGenes(); i++) {			
			LinEquation* NewUpperConstraint = InitializeLinEquation("Gene-reaction mapping of new upper constraint",0,LESS);
			NewUpperConstraint->Variables.push_back(InData->GetGene(i)->GetMFAVar());
			NewUpperConstraint->Coefficient.push_back(1);
			
			for (int j=0; j < InData->GetGene(i)->FNumReactions(); j++ ) {
				Reaction* rxn = InData->GetGene(i)->GetReaction(j);
				MFAVariable* ReactionUseVariable = rxn->GetMFAVar(REACTION_USE);
				if (ReactionUseVariable != NULL) {
					NewUpperConstraint->Variables.push_back(ReactionUseVariable);
					NewUpperConstraint->Coefficient.push_back(-1);
				} else {
					ReactionUseVariable = rxn->GetMFAVar(FORWARD_USE);
					if (ReactionUseVariable != NULL) {
						NewUpperConstraint->Variables.push_back(ReactionUseVariable);
						NewUpperConstraint->Coefficient.push_back(-1);
					}
					ReactionUseVariable = rxn->GetMFAVar(REVERSE_USE);
					if (ReactionUseVariable != NULL) {
						NewUpperConstraint->Variables.push_back(ReactionUseVariable);
						NewUpperConstraint->Coefficient.push_back(-1);
					}		      
				}		    
			}
			AddConstraint(NewUpperConstraint);
		}
	}

	//Adding variables and constraints associated with deletion optimizations
	if (InParameters->GeneConstraints && InParameters->DeletionOptimization) {
		InData->AutomaticallyCreateGeneIntervals(InParameters);
		Gene* StartingGene = NULL;
		for (int i=0; i < InData->FNumGenes(); i++) {
			//Creating a gene variable for each gene not covered by a gene interval
			if (InData->GetGene(i)->FNumIntervals() == 0 && InData->GetGene(i)->GetMFAVar() == NULL) {
				if (StartingGene == NULL) {
					StartingGene = InData->GetGene(i);
				}
				AddVariable(InData->GetGene(i)->CreateMFAVariable(InParameters));
			}
		}
		//Creating and adding interval variables
		for (int i=0; i < InData->FNumGeneIntervals(); i++) {
			AddVariable(InData->GetGeneInterval(i)->CreateMFAVariable(InParameters));
		}
		//Creating and adding constraints to preclude the simultaneous deletion of experimentally determined coessential genes
		for (int i=0; i < int(InParameters->UnviableIntervalCombinations.size()); i++) {
			LinEquation* NewConstraint = InitializeLinEquation("Eliminating unviable gene combination",1,GREATER,LINEAR);
			for (int j=0; j < int(InParameters->UnviableIntervalCombinations[i].size()); j++) {
				Gene* CurrentGene = InData->FindGene("DATABASE;NAME",InParameters->UnviableIntervalCombinations[i][j].data());
				//If an gene is not found, then this constraint is invalid and should be deleted
				if (CurrentGene == NULL) {
					delete NewConstraint;
					NewConstraint = NULL;
					break;
				} else {
					NewConstraint->Variables.push_back(CurrentGene->GetMFAVar());
					NewConstraint->Coefficient.push_back(1);
				}
			}
			if (NewConstraint != NULL) {
				AddConstraint(NewConstraint);
			}
		}
		//Creating cut variables and constraints
		Gene* CurrentGene = StartingGene;
		do {
			//Adding a cut variable and constraint so long as the two neighboring genes are not in the same interval
			if (CurrentGene->FNumIntervals() == 0 || CurrentGene->NextGene()->FNumIntervals() == 0 || CurrentGene->GetInterval(0) != CurrentGene->NextGene()->GetInterval(0)) {
				//Creating two cut variables for each direction of the cut
				MFAVariable* CutOne = InitializeMFAVariable();
				CutOne->UpperBound = 1;
				CutOne->LowerBound = 0;
				CutOne->Binary = true;
				CutOne->AssociatedGene = CurrentGene;
				CutOne->Type = GENOME_CUTS;
				AddVariable(CutOne);
				MFAVariable* CutTwo = InitializeMFAVariable();
				CutTwo->UpperBound = 1;
				CutTwo->LowerBound = 0;
				CutTwo->Binary = true;
				CutTwo->AssociatedGene = CurrentGene;
				CutTwo->Type = GENOME_CUTS;
				AddVariable(CutTwo);
				//Creating the cut constraint
				LinEquation* CutConstraint =  InitializeLinEquation("Enforcing the cut variable values");
				CutConstraint->Coefficient.push_back(1);
				CutConstraint->Variables.push_back(CutOne);
				CutConstraint->Coefficient.push_back(1);
				if (CurrentGene->FNumIntervals() == 0) {
					CutConstraint->Variables.push_back(CurrentGene->GetMFAVar());
				} else {
					CutConstraint->Variables.push_back(CurrentGene->GetInterval(0)->GetMFAVar());
				}
				CutConstraint->Coefficient.push_back(-1);
				if (CurrentGene->NextGene()->FNumIntervals() == 0) {
					CutConstraint->Variables.push_back(CurrentGene->NextGene()->GetMFAVar());
				} else {
					CutConstraint->Variables.push_back(CurrentGene->NextGene()->GetInterval(0)->GetMFAVar());
				}
				CutConstraint->Coefficient.push_back(-1);
				CutConstraint->Variables.push_back(CutTwo);
				AddConstraint(CutConstraint);
				//Creating the constraint that prevents CutOne and CutTwo from both being one
				CutConstraint =  InitializeLinEquation("Ensuring that CutOne and CutTwo are not simultaneously one",1,LESS);
				CutConstraint->Coefficient.push_back(1);
				CutConstraint->Variables.push_back(CutOne);
				CutConstraint->Coefficient.push_back(1);
				CutConstraint->Variables.push_back(CutTwo);
				AddConstraint(CutConstraint);
			}
			//Iterating to the next gene
			CurrentGene = CurrentGene->NextGene();
		} while (CurrentGene != StartingGene);
		//Creating the constraint fixing the number of deletions
		LinEquation* DeletionsConstraint = AddSumConstraint(GENOME_CUTS, false, 1, 2*atof(GetParameter("Maximum number of deletions").data()), LESS);
		DeletionsConstraint->ConstraintMeaning.assign("Constraint on the maximum number of deletions allowed");
	}
	
	//Adding variables and constraints associated with intervals (INTERVAL USE)
	if (InParameters->GeneConstraints && InParameters->IntervalOptimization) {
		//Loading the gene intervals if they are not already loaded
		if (InData->FNumGeneIntervals() == 0) {
			InData->LoadGeneIntervals();
		}
		//Creating and adding interval variables
		for (int i=0; i < InData->FNumGeneIntervals(); i++) {
			AddVariable(InData->GetGeneInterval(i)->CreateMFAVariable(InParameters));
		}

		//Creating and adding interval/gene constraints
		for (int i=0; i < InData->FNumGenes(); i++) {
			LinEquation* NewIntervalConstraint = InData->GetGene(i)->CreateIntervalDeletionConstraint();
			//Interval constraints could be null if no genes in the interval are associated with at least one reaction
			if (NewIntervalConstraint != NULL) {
				AddConstraint(NewIntervalConstraint);
			}
		}
		
		//Creating and adding constraints to preclude the simultaneous deletion of experimentally determined coessential intervals
		for (int i=0; i < int(InParameters->UnviableIntervalCombinations.size()); i++) {
			LinEquation* NewConstraint = InitializeLinEquation("Eliminating unviable interval combination",1,GREATER,LINEAR);
			for (int j=0; j < int(InParameters->UnviableIntervalCombinations[i].size()); j++) {
				GeneInterval* CurrentInterval = InData->FindInterval(InParameters->UnviableIntervalCombinations[i][j]);
				//If an interval is not found, then this constraint is invalid and should be deleted
				if (CurrentInterval == NULL) {
					delete NewConstraint;
					NewConstraint = NULL;
					break;
				} else {
					NewConstraint->Variables.push_back(CurrentInterval->GetMFAVar());
					NewConstraint->Coefficient.push_back(1);
				}
			}
			if (NewConstraint != NULL) {
				AddConstraint(NewConstraint);
			}
		}
	}
	
	//Adding atom uptake constraints
	if (GetParameter("uptake limits").compare("none") != 0) {
		vector<string>* strings = StringToStrings(GetParameter("uptake limits"),";");
		for (int i=0; i < int(strings->size()); i++) {
			vector<string>* stringsTwo = StringToStrings((*strings)[i],":");
			if (stringsTwo->size() >= 2) {
				LinEquation* newConstraint = InitializeLinEquation("Uptake constraint",atof((*stringsTwo)[1].data()),LESS);
				for (int j=0; j < this->FNumVariables();j++) {
					MFAVariable* currVar = this->GetVariable(j);
					if ((currVar->Type == FORWARD_DRAIN_FLUX || currVar->Type == DRAIN_FLUX) && currVar->AssociatedSpecies != NULL) {
						int atomCount = currVar->AssociatedSpecies->CountAtomType((*stringsTwo)[0].data());
						newConstraint->Variables.push_back(currVar);
						newConstraint->Coefficient.push_back(atomCount);
					}
				}
				this->AddConstraint(newConstraint);
			}
			delete stringsTwo;
		}
		delete strings;
	}

	return SUCCESS;
}

int MFAProblem::BuildDualMFAProblem(MFAProblem* InProblem, Data* InData, OptimizationParameter*& InParameters) {
	//Clearing any existing problem
	if (Variables.size() > 0 || Constraints.size() > 0) {
		ClearObjective();
		ClearConstraints();
		ClearVariables();
	}

	//Checking that the input primal problem is built
	if (InProblem->FNumVariables() == 0) {
		InProblem->BuildMFAProblem(InData,InParameters);
	}
	
	//Setting the dual objective
	LinEquation* NewObjective = InitializeLinEquation();
	//Creating dual variables for each primal constraint
	for (int i=0; i < InProblem->FNumConstraints(); i++) {
		if (InProblem->GetConstraint(i)->ConstraintMeaning.compare("Enforcing use variable") != 0 && InProblem->GetConstraint(i)->ConstraintMeaning.compare("Preventing simultaneous operation of forward and backward reactions") != 0) {
			MFAVariable* DualVariable = InitializeMFAVariable();
			DualVariable->Name = "Dual variable for constraint "+InProblem->GetConstraint(i)->ConstraintMeaning;
			DualVariable->DualConstraint = InProblem->GetConstraint(i);
			DualVariable->Primal = false;
			AddVariable(DualVariable);
			InProblem->GetConstraint(i)->DualVariable = DualVariable;
			DualVariable->UpperBound = 10000;
			DualVariable->LowerBound = -10000;
			if (InProblem->GetConstraint(i)->EqualityType == LESS) {
				DualVariable->LowerBound = 0;
			} else if (InProblem->GetConstraint(i)->EqualityType == GREATER) {
				DualVariable->UpperBound = 0;
			}
			if (InProblem->GetConstraint(i)->RightHandSide != 0) {
				NewObjective->Variables.push_back(DualVariable);
				NewObjective->Coefficient.push_back(InProblem->GetConstraint(i)->RightHandSide);
			}
		}
	}
	AddObjective(NewObjective);

	//Creating dual constraints for each primal variable
	for (int i=0; i < InProblem->FNumVariables(); i++) {
		LinEquation* DualConstraint = NULL;
		if (!InProblem->GetVariable(i)->Binary) {
			DualConstraint = InitializeLinEquation();
			DualConstraint->ConstraintMeaning = "Dual constraint for "+InProblem->GetVariable(i)->Name;
			InProblem->GetVariable(i)->DualConstraint = DualConstraint;
			DualConstraint->Primal = false;
			DualConstraint->DualVariable = InProblem->GetVariable(i);
			AddConstraint(DualConstraint);
			//if (InProblem->GetVariable(i)->UpperBound <= 0) {
				//DualConstraint->EqualityType = LESS;
			//} else if (InProblem->GetVariable(i)->LowerBound >= 0) {
			if (InProblem->GetVariable(i)->LowerBound >= 0) {
				DualConstraint->EqualityType = GREATER;
			} else {
				DualConstraint->EqualityType = EQUAL;
			}
			//if (InProblem->GetVariable(i)->UpperBound != FLAG && InProblem->GetVariable(i)->UpperBound != 0) {
				MFAVariable* DualVariable = InitializeMFAVariable();
				DualVariable->Name = "Upper bound dual variable for flux "+InProblem->GetVariable(i)->Name;
				DualVariable->UpperBoundDualVariable = InProblem->GetVariable(i);
				InProblem->GetVariable(i)->UpperBoundDualVariable = DualVariable;
				DualVariable->Primal = false;
				AddVariable(DualVariable);
				DualVariable->UpperBound = 10000;
				DualVariable->LowerBound = 0;
				DualConstraint->Variables.push_back(DualVariable);
				DualConstraint->Coefficient.push_back(1);
				NewObjective->Variables.push_back(DualVariable);
				NewObjective->Coefficient.push_back(InProblem->GetVariable(i)->UpperBound);	
			//}
			if (InProblem->GetVariable(i)->LowerBound != FLAG && InProblem->GetVariable(i)->LowerBound > 0) {
				MFAVariable* DualVariable = InitializeMFAVariable();
				DualVariable->LowerBoundDualVariable = InProblem->GetVariable(i);
				DualVariable->Name = "Lower bound dual variable for flux "+InProblem->GetVariable(i)->Name;
				InProblem->GetVariable(i)->LowerBoundDualVariable = DualVariable;
				DualVariable->Primal = false;
				AddVariable(DualVariable);
				DualVariable->UpperBound = 0;
				DualVariable->LowerBound = -10000;
				DualConstraint->Variables.push_back(DualVariable);
				DualConstraint->Coefficient.push_back(1);
				NewObjective->Variables.push_back(DualVariable);
				NewObjective->Coefficient.push_back(InProblem->GetVariable(i)->LowerBound);	
			}
		} else if (InProblem->GetVariable(i)->Binary && (InProblem->GetVariable(i)->Type == REACTION_USE || InProblem->GetVariable(i)->Type == DRAIN_USE || InProblem->GetVariable(i)->Type == FORWARD_USE || InProblem->GetVariable(i)->Type == REVERSE_USE || InProblem->GetVariable(i)->Type == FORWARD_DRAIN_USE || InProblem->GetVariable(i)->Type == REVERSE_DRAIN_USE)) {
			AddVariable(InProblem->GetVariable(i));
		}
	}
	//Setting the right hand side of each dual constraint based on primal objective
	LinEquation* PrimalObjective = InProblem->GetObjective();
	for (int i=0; i < int(PrimalObjective->Variables.size()); i++) {
		if (PrimalObjective->Variables[i]->DualConstraint != NULL) {
			PrimalObjective->Variables[i]->DualConstraint->RightHandSide = PrimalObjective->Coefficient[i];
		}
	}

	//Adding the dual variables to the dual constraints with the proper coefficients
	for (int i=0; i < InProblem->FNumConstraints(); i++) {
		if (InProblem->GetConstraint(i)->DualVariable != NULL) {
			for (int j=0; j < int(InProblem->GetConstraint(i)->Variables.size()); j++) {
				if (InProblem->GetConstraint(i)->Variables[j]->DualConstraint != NULL) {
					InProblem->GetConstraint(i)->Variables[j]->DualConstraint->Variables.push_back(InProblem->GetConstraint(i)->DualVariable);
					InProblem->GetConstraint(i)->Variables[j]->DualConstraint->Coefficient.push_back(InProblem->GetConstraint(i)->Coefficient[j]);
				}
			}
		}
		//Creating a constraint to enforce the use variable
		if (InProblem->GetConstraint(i)->ConstraintMeaning.compare("Enforcing use variable") == 0) {
			LinEquation* DualConstraint = NULL;
			MFAVariable* BinaryVariable = NULL;
			vector<MFAVariable*> NonbinaryVariables;
			for (int j=0; j < int(InProblem->GetConstraint(i)->Variables.size()); j++) {
				if (!InProblem->GetConstraint(i)->Variables[j]->Binary) {
					DualConstraint = InProblem->GetConstraint(i)->Variables[j]->DualConstraint;
					if (InProblem->GetConstraint(i)->Variables[j]->UpperBoundDualVariable != NULL) {
						NonbinaryVariables.push_back(InProblem->GetConstraint(i)->Variables[j]->UpperBoundDualVariable);
					}
					if (InProblem->GetConstraint(i)->Variables[j]->LowerBoundDualVariable != NULL) {
						NonbinaryVariables.push_back(InProblem->GetConstraint(i)->Variables[j]->LowerBoundDualVariable);
					}
				} else {
					BinaryVariable = InProblem->GetConstraint(i)->Variables[j];					
				}
			}
			if (DualConstraint->EqualityType == LESS) {
				DualConstraint->Variables.push_back(BinaryVariable);
				DualConstraint->Coefficient.push_back(10000);
				DualConstraint->RightHandSide = DualConstraint->RightHandSide + 10000;
			} else if (DualConstraint->EqualityType == GREATER) {
				DualConstraint->Coefficient.push_back(-10000);
				DualConstraint->Variables.push_back(BinaryVariable);
				DualConstraint->RightHandSide = DualConstraint->RightHandSide - 10000;
			}
			for (int j=0; j < int(NonbinaryVariables.size()); j++) {
				LinEquation* DualConstraint = InitializeLinEquation();
				DualConstraint->ConstraintMeaning = "Dual variable enforcing use variable";
				DualConstraint->Primal = false;
				DualConstraint->DualConstraint = InProblem->GetConstraint(i);
				InProblem->GetConstraint(i)->DualConstraint = DualConstraint;
				AddConstraint(DualConstraint);
				DualConstraint->RightHandSide = 0;
				DualConstraint->EqualityType = LESS;
				DualConstraint->Variables.push_back(NonbinaryVariables[j]);
				DualConstraint->Coefficient.push_back(1);
				DualConstraint->Variables.push_back(BinaryVariable);
				DualConstraint->Coefficient.push_back(-10000);
			}			
		}
	}

	//Setting the sense for the dual problem
	if (InProblem->FMax()) {
		SetMin();
	} else {
		SetMax();
	}

	//Reindexing variables and constraints
	ResetIndecies();

	return SUCCESS;
}

LinEquation* MFAProblem::CreateUseVariableConstraint(MFAVariable* InVariable,OptimizationParameter*& InParameters) {
	LinEquation* NewConstraint = InitializeLinEquation();
	NewConstraint->AssociatedSpecies = InVariable->AssociatedSpecies;
	NewConstraint->AssociatedReaction = InVariable->AssociatedReaction;
	NewConstraint->EqualityType = LESS;
	NewConstraint->RightHandSide = 0;
	NewConstraint->ConstraintMeaning.assign("Enforcing use variable");
	NewConstraint->Coefficient.push_back(1);
	
	MFAVariable* FluxVariable = NULL;
	if (InVariable->AssociatedSpecies != NULL && InVariable->Type == DRAIN_USE) {
		FluxVariable = InVariable->AssociatedSpecies->GetMFAVar(DRAIN_FLUX,InVariable->Compartment);
	} else if (InVariable->AssociatedSpecies != NULL && InVariable->Type == FORWARD_DRAIN_USE) {
		FluxVariable = InVariable->AssociatedSpecies->GetMFAVar(FORWARD_DRAIN_FLUX,InVariable->Compartment);
	} else if (InVariable->AssociatedSpecies != NULL && InVariable->Type == REVERSE_DRAIN_USE) {
		FluxVariable = InVariable->AssociatedSpecies->GetMFAVar(REVERSE_DRAIN_FLUX,InVariable->Compartment);
	} else if (InVariable->AssociatedReaction != NULL && InVariable->Type == REACTION_USE) {
		FluxVariable = InVariable->AssociatedReaction->GetMFAVar(FLUX);
		if (FluxVariable == NULL) {
			FluxVariable = InVariable->AssociatedReaction->GetMFAVar(FORWARD_FLUX);
		}
	} else if (InVariable->AssociatedReaction != NULL && InVariable->Type == FORWARD_USE) {
		FluxVariable = InVariable->AssociatedReaction->GetMFAVar(FORWARD_FLUX);
		if (FluxVariable == NULL) {
			FluxVariable = InVariable->AssociatedReaction->GetMFAVar(FLUX);
		}
	} else if (InVariable->AssociatedReaction != NULL && InVariable->Type == REVERSE_USE) {
		FluxVariable = InVariable->AssociatedReaction->GetMFAVar(REVERSE_FLUX);
		if (FluxVariable == NULL) {
			FluxVariable = InVariable->AssociatedReaction->GetMFAVar(FLUX);
		}
	}
	
	if (FluxVariable == NULL) {
		delete NewConstraint;
		return NULL;
	}

	NewConstraint->Variables.push_back(FluxVariable);
	if (FluxVariable->UpperBound > 100) {
		NewConstraint->Coefficient.push_back(-FluxVariable->UpperBound);
	} else {
		NewConstraint->Coefficient.push_back(-100);
	}
	NewConstraint->Variables.push_back(InVariable);

	return NewConstraint;
}

LinEquation* MFAProblem::CreateUseVariablePositiveConstraint(MFAVariable* InVariable,OptimizationParameter*& InParameters) {
	LinEquation* NewConstraint = InitializeLinEquation();
	NewConstraint->AssociatedSpecies = InVariable->AssociatedSpecies;
	NewConstraint->AssociatedReaction = InVariable->AssociatedReaction;
	NewConstraint->EqualityType = GREATER;
	NewConstraint->RightHandSide = 0;
	NewConstraint->ConstraintMeaning.assign("Enforcing use variable");
	double minFlux = atof(GetParameter("Minimum flux for use variable positive constraint").data());
	NewConstraint->Coefficient.push_back(-minFlux);
	NewConstraint->Variables.push_back(InVariable);
	NewConstraint->Coefficient.push_back(1);
	MFAVariable* FluxVariable = NULL;
	if (InVariable->AssociatedSpecies != NULL && InVariable->Type == DRAIN_USE) {
		FluxVariable = InVariable->AssociatedSpecies->GetMFAVar(DRAIN_FLUX,InVariable->Compartment);
	} else if (InVariable->AssociatedSpecies != NULL && InVariable->Type == FORWARD_DRAIN_USE) {
		FluxVariable = InVariable->AssociatedSpecies->GetMFAVar(FORWARD_DRAIN_FLUX,InVariable->Compartment);
	} else if (InVariable->AssociatedSpecies != NULL && InVariable->Type == REVERSE_DRAIN_USE) {
		FluxVariable = InVariable->AssociatedSpecies->GetMFAVar(REVERSE_DRAIN_FLUX,InVariable->Compartment);
	} else if (InVariable->AssociatedReaction != NULL && InVariable->Type == REACTION_USE) {
		FluxVariable = InVariable->AssociatedReaction->GetMFAVar(FLUX);
		if (FluxVariable == NULL) {
			FluxVariable = InVariable->AssociatedReaction->GetMFAVar(FORWARD_FLUX);
		}
	} else if (InVariable->AssociatedReaction != NULL && InVariable->Type == FORWARD_USE) {
		FluxVariable = InVariable->AssociatedReaction->GetMFAVar(FORWARD_FLUX);
		if (FluxVariable == NULL) {
			FluxVariable = InVariable->AssociatedReaction->GetMFAVar(FLUX);
		}
	} else if (InVariable->AssociatedReaction != NULL && InVariable->Type == REVERSE_USE) {
		FluxVariable = InVariable->AssociatedReaction->GetMFAVar(REVERSE_FLUX);
		if (FluxVariable == NULL) {
			FluxVariable = InVariable->AssociatedReaction->GetMFAVar(FLUX);
		}
	}
	if (FluxVariable == NULL) {
		delete NewConstraint;
		return NULL;
	}
	NewConstraint->Variables.push_back(FluxVariable);
	return NewConstraint;
}

LinEquation* MFAProblem::CreateGibbsEnergyConstraint(Reaction* InReaction, OptimizationParameter*& InParameters) {	
	//Checking for a flux or forward flux variable and adding feasibility constraint
	MFAVariable* FluxVar = InReaction->GetMFAVar(FORWARD_FLUX);
	if (FluxVar == NULL) {
		FluxVar = InReaction->GetMFAVar(FLUX);
	}
	if (FluxVar != NULL) {
		LinEquation* NewConstraint = InitializeLinEquation("thermo feasibility constraint",MFA_MAX_FEASIBLE_DELTAG,LESS);
		NewConstraint->AssociatedReaction = InReaction;
		NewConstraint->Coefficient.push_back(1);
		NewConstraint->Variables.push_back(InReaction->GetMFAVar(DELTAG));
		MFAVariable* UseVar = InReaction->GetMFAVar(FORWARD_USE);
		if (UseVar == NULL) {
			UseVar = InReaction->GetMFAVar(REACTION_USE);
		}
		if (UseVar != NULL) {
			//Adding the use variable to the constraint
			NewConstraint->Coefficient.push_back(MFA_THERMO_CONST);
			NewConstraint->RightHandSide += MFA_THERMO_CONST;
			NewConstraint->Variables.push_back(UseVar);
			AddConstraint(NewConstraint);
		} else if (FluxVar->LowerBound > 0) {
			//The reaction is always active in the forward direction meaning that its delta G must always be negative
			AddConstraint(NewConstraint);
		} else {
			//The reaction is always inactive meaning that it should not have a thermofeasibility constraint
			delete NewConstraint;
		}
	}
	FluxVar = InReaction->GetMFAVar(REVERSE_FLUX);
	if (FluxVar != NULL) {
		LinEquation* NewConstraint = InitializeLinEquation("reverse thermo feasibility constraint",MFA_MAX_FEASIBLE_DELTAG,LESS);
		NewConstraint->AssociatedReaction = InReaction;
		NewConstraint->Coefficient.push_back(-1);
		NewConstraint->Variables.push_back(InReaction->GetMFAVar(DELTAG));
		MFAVariable* UseVar = InReaction->GetMFAVar(REVERSE_USE);
		if (UseVar != NULL) {
			//Adding the use variable to the constraint
			NewConstraint->Coefficient.push_back(MFA_THERMO_CONST);
			NewConstraint->RightHandSide += MFA_THERMO_CONST;
			NewConstraint->Variables.push_back(UseVar);
			AddConstraint(NewConstraint);
		} else if (FluxVar->LowerBound > 0) {
			//The reaction is always active in the forward direction meaning that its delta G must always be negative
			AddConstraint(NewConstraint);
		} else {
			//The reaction is always inactive meaning that it should not have a thermofeasibility constraint
			delete NewConstraint;
		}
	}

	//Adding the chemical potential thermodynamic constraint
	LinEquation* NewConstraint = InitializeLinEquation("gibbs energy constraint");
	NewConstraint->AssociatedReaction = InReaction;
	NewConstraint->Coefficient.push_back(-1);
	NewConstraint->Variables.push_back(InReaction->GetMFAVar(DELTAG));
	for (int j=0; j < InReaction->FNumReactants(); j++) {
		//First I add the concentration terms to the Gibb energy constraint for all species that are no water or hydrogen
		if (InReaction->GetReactant(j)->FFormula().compare("H") != 0) {
			NewConstraint->Coefficient.push_back(InReaction->GetReactantCoef(j));
			NewConstraint->Variables.push_back(InReaction->GetReactant(j)->GetMFAVar(POTENTIAL,InReaction->GetReactantCompartment(j)));
		}
	}

	//Here I add the appropriate terms for any transport across a cell membrane
	double ConstantTerm;
	vector<int> Compartments;
	vector<double> TransportCoefficients = InReaction->GetTransportCoefficient(Compartments,ConstantTerm,InParameters);
	if (GetParameter("allow pH to vary").compare("1") == 0 && InReaction->FMainData()->GetHydrogenSpecies() != NULL) {
		//I need to find the species corresponding to the hydrogen ion in this dataset
		NewConstraint->RightHandSide += -ConstantTerm;
		for (int j=0; j < int(Compartments.size()); j ++) {
			MFAVariable* HVar = InReaction->FMainData()->GetHydrogenSpecies()->GetMFAVar(LOG_CONC,Compartments[j]);
			NewConstraint->Variables.push_back(HVar);
			NewConstraint->Coefficient.push_back(TransportCoefficients[j]);
		}
	} else {
		NewConstraint->RightHandSide += -ConstantTerm;
		for (int j=0; j < int(Compartments.size()); j ++) {
			NewConstraint->RightHandSide += -TransportCoefficients[j]*log(pow(10,-GetCompartment(Compartments[j])->pH));
		}
	}

	return NewConstraint;
}

LinEquation* MFAProblem::CreateReactionErrorConstraint(Reaction* InReaction, OptimizationParameter*& InParameters) {	
	//Adding the constraint that enforces reaction error use variables
	if (InParameters->ReactionErrorUseVariables) {
		LinEquation* NewConstraint = InitializeLinEquation("reaction deltaG error use variable constraint",0,GREATER);
		NewConstraint->AssociatedReaction = InReaction;
		//Adding the positive and negative deltaG error
		NewConstraint->Coefficient.push_back(-1);
		NewConstraint->Variables.push_back(InReaction->GetMFAVar(REACTION_DELTAG_PERROR));
		NewConstraint->Coefficient.push_back(-1);
		NewConstraint->Variables.push_back(InReaction->GetMFAVar(REACTION_DELTAG_NERROR));
		NewConstraint->Coefficient.push_back(100000);
		NewConstraint->Variables.push_back(InReaction->GetMFAVar(SMALL_DELTAG_ERROR_USE));
		//Adding the constraint
		AddConstraint(NewConstraint);
		NewConstraint = InitializeLinEquation("reaction deltaG error decomposition",0);
		NewConstraint->AssociatedReaction = InReaction;
		//Adding the positive and negative deltaG error
		NewConstraint->Coefficient.push_back(1);
		NewConstraint->Variables.push_back(InReaction->GetMFAVar(REACTION_DELTAG_PERROR));
		NewConstraint->Coefficient.push_back(-1);
		NewConstraint->Variables.push_back(InReaction->GetMFAVar(REACTION_DELTAG_NERROR));
		NewConstraint->Coefficient.push_back(-1);
		NewConstraint->Variables.push_back(InReaction->GetMFAVar(REACTION_DELTAG_ERROR));
		//Adding the constraint
		AddConstraint(NewConstraint);
	}
	//Creating the constraint that sets the reaction deltaG error to the sum of the reactant deltaG errors
	LinEquation* NewConstraint = InitializeLinEquation("reaction deltaG error constraint");
	NewConstraint->AssociatedReaction = InReaction;
	NewConstraint->Coefficient.push_back(-1);
	NewConstraint->Variables.push_back(InReaction->GetMFAVar(REACTION_DELTAG_ERROR));
	//Next I add the error terms to account for uncertainty in the estimate Gibbs free energy
	map<Species* , double , std::less<Species*> > TotalCoefficients;
	//I initialize my map to zero
	for (int j=0; j < InReaction->FNumReactants(); j++) {
		if (InReaction->GetReactant(j)->FFormula().compare("H") != 0) {
			TotalCoefficients[InReaction->GetReactant(j)] = 0;
		}
	}
	//Now I determine the net stoichiometry for each distinct species involved in the reaction
	for (int j=0; j < InReaction->FNumReactants(); j++) {
		if (InReaction->GetReactant(j)->FFormula().compare("H") != 0) {
			TotalCoefficients[InReaction->GetReactant(j)] += InReaction->GetReactantCoef(j);
		}
	}
	//Now we add uncertainty terms for each distinct formation energy
	for (map<Species* , double , std::less<Species*> >::iterator MapIT = TotalCoefficients.begin(); MapIT != TotalCoefficients.end(); MapIT++) {
		if (MapIT->second != 0) {
			NewConstraint->Coefficient.push_back(MapIT->second);
			NewConstraint->Variables.push_back(MapIT->first->GetMFAVar(DELTAGF_PERROR));
			NewConstraint->Coefficient.push_back(-MapIT->second);
			NewConstraint->Variables.push_back(MapIT->first->GetMFAVar(DELTAGF_NERROR));
		}
	}
	return NewConstraint;
}

void MFAProblem::CreateSpeciesGibbsEnergyConstraint(Species* InSpecies, OptimizationParameter*& InParameters) {
	//Pulling the list of variables for this species
	vector<MFAVariable*> Variables;
	InSpecies->GetAllMFAVariables(Variables);
	//Creating a potential constraint for each potential variable found
	for (int i=0; i < int(Variables.size()); i++) {
		if (Variables[i]->Type == POTENTIAL) {			
			//Creating the chemical potential constraint
			LinEquation* NewConstraint = InitializeLinEquation("chemical potential constraint");
			NewConstraint->AssociatedSpecies = InSpecies;
			//Adding the chemical potential variable
			NewConstraint->Coefficient.push_back(1);
			NewConstraint->Variables.push_back(Variables[i]);
			//Searching for and adding the concentration variable
			for (int j=0; j < int(Variables.size()); j++) {
				if (Variables[j]->Type == LOG_CONC && Variables[j]->Compartment == Variables[i]->Compartment) {
					NewConstraint->Coefficient.push_back(-1*GAS_CONSTANT*InParameters->Temperature);
					NewConstraint->Variables.push_back(Variables[j]);
					break;
				}
			}
			//Adding the error variables if necessary
			if (InParameters->DeltaGError) {
				NewConstraint->Coefficient.push_back(-1);
				NewConstraint->Variables.push_back(InSpecies->GetMFAVar(DELTAGF_PERROR));
				NewConstraint->Coefficient.push_back(1);
				NewConstraint->Variables.push_back(InSpecies->GetMFAVar(DELTAGF_NERROR));
			}
			//If the energy is known, we add the deltaG to the RHS and we're done
			if (InSpecies->FEstDeltaG() != FLAG) {
				NewConstraint->RightHandSide = InSpecies->AdjustedDeltaG(GetCompartment(Variables[i]->Compartment)->IonicStrength,GetCompartment(Variables[i]->Compartment)->pH,InParameters->Temperature);
			} else {
				//Adding all known group energies to the RHS and adding all unknown group energies as variables
				NewConstraint->RightHandSide = InSpecies->AdjustedDeltaG(GetCompartment(Variables[i]->Compartment)->IonicStrength,GetCompartment(Variables[i]->Compartment)->pH,InParameters->Temperature)-FLAG;
				for (int j=0; j < InSpecies->FNumStructuralCues(); j++) {
					if (InSpecies->GetStructuralCue(j)->FEstDeltaG() == -10000) {
						NewConstraint->Coefficient.push_back(-InSpecies->GetStructuralCueNum(j));
						NewConstraint->Variables.push_back(InSpecies->GetStructuralCue(j)->GetMFAVar(DELTAGG_ENERGY));
					} else {
						NewConstraint->RightHandSide = NewConstraint->RightHandSide + InSpecies->GetStructuralCue(j)->FEstDeltaG()*InSpecies->GetStructuralCueNum(j);
					}
				}
			}
			AddConstraint(NewConstraint);
		}
	}
}

LinEquation* MFAProblem::ConvertStringToObjective(string ObjString, Data* InData) {
	int Count = 0;
	vector<string>* ObjectiveTerms = StringToStrings(ObjString,":;");
	LinEquation* NewObjective = InitializeLinEquation();
	vector<int> SumTypes;
	vector<double> SumCoeff;
	vector<bool> QuadSum;
	while (int(ObjectiveTerms->size()) > Count) {
		if ((*ObjectiveTerms)[Count].compare("MAX") == 0) {
			Count += 1;
			SetMax();
		} else if ((*ObjectiveTerms)[Count].compare("MIN") == 0) {
			Count += 1;
			SetMin();
		} else if ((*ObjectiveTerms)[Count].compare("SUM") == 0 || (*ObjectiveTerms)[Count].compare("QUADSUM") == 0) {
			if (int(ObjectiveTerms->size()-2) <= Count) {
				FErrorFile() << "Syntax error in problem objective" << endl;
				FlushErrorFile();
				return NULL;
			}

			SumTypes.push_back(ConvertVariableType((*ObjectiveTerms)[Count+1]));
			SumCoeff.push_back(atof((*ObjectiveTerms)[Count+2].data()));
			if ((*ObjectiveTerms)[Count].compare("QUADSUM") == 0) {
				QuadSum.push_back(true);
			} else {
				QuadSum.push_back(false);
			}
			Count += 3;
		} else {
			int VarType = ConvertVariableType((*ObjectiveTerms)[Count]);
			if (VarType == -1) {
				return NULL;
			}
			
			if (int(ObjectiveTerms->size()-3) <= Count) {
				FErrorFile() << "Syntax error in problem objective" << endl;
				FlushErrorFile();
				return NULL;
			}

			if (VarType == FLUX || VarType == FORWARD_FLUX || VarType == REVERSE_FLUX || VarType == DELTAG) {
				Reaction* Temp = InData->FindReaction("NAME;DATABASE;ENTRY",(*ObjectiveTerms)[Count+1].data());
				if (Temp != NULL) {
					MFAVariable* NewVariable = Temp->GetMFAVar(VarType);
					if (NewVariable != NULL) {
						NewObjective->Variables.push_back(NewVariable);
						NewObjective->Coefficient.push_back(atof((*ObjectiveTerms)[Count+3].data()));
					} else {
						if (VarType == FLUX) {
							NewVariable = Temp->GetMFAVar(FORWARD_FLUX);
							if (NewVariable != NULL) {
								NewObjective->Variables.push_back(NewVariable);
								NewObjective->Coefficient.push_back(atof((*ObjectiveTerms)[Count+3].data()));
							}
							NewVariable = Temp->GetMFAVar(REVERSE_FLUX);
							if (NewVariable != NULL) {
								NewObjective->Variables.push_back(NewVariable);
								NewObjective->Coefficient.push_back(-atof((*ObjectiveTerms)[Count+3].data()));
							}
						} else {
							FErrorFile() << (*ObjectiveTerms)[Count+1] << " has no " << (*ObjectiveTerms)[Count] << " variable type." << endl;
							FlushErrorFile();
							return NULL;
						}
					}		
				} else {
					FErrorFile() << (*ObjectiveTerms)[Count+1] << " variable not found." << endl;
					FlushErrorFile();
					return NULL;
				}
			} else if (VarType == CONC || VarType == LOG_CONC || VarType == DRAIN_FLUX || VarType == FORWARD_DRAIN_FLUX || VarType == REVERSE_DRAIN_FLUX) {
				Species* Temp = InData->FindSpecies("NAME;DATABASE;ENTRY",(*ObjectiveTerms)[Count+1].data());
				if (Temp != NULL) {
					MFAVariable* NewVariable = NULL;
					if ((*ObjectiveTerms)[Count+2].compare("none") == 0) {
						NewVariable = Temp->GetMFAVar(VarType);
						if (NewVariable != NULL) {
							NewObjective->Variables.push_back(NewVariable);
							NewObjective->Coefficient.push_back(atof((*ObjectiveTerms)[Count+3].data()));
						}
					} else {
						CellCompartment* Comp = GetCompartment((*ObjectiveTerms)[Count+2].data());
						if (Comp != NULL) {
							NewVariable = Temp->GetMFAVar(VarType, Comp->Index);
							if (NewVariable != NULL) {
								NewObjective->Variables.push_back(NewVariable);
								NewObjective->Coefficient.push_back(atof((*ObjectiveTerms)[Count+3].data()));
							} else if (VarType == DRAIN_FLUX) {
								NewVariable = Temp->GetMFAVar(FORWARD_DRAIN_FLUX, Comp->Index);
								if (NewVariable != NULL) {
									NewObjective->Variables.push_back(NewVariable);
									NewObjective->Coefficient.push_back(atof((*ObjectiveTerms)[Count+3].data()));
								}
								NewVariable = Temp->GetMFAVar(REVERSE_DRAIN_FLUX, Comp->Index);
								if (NewVariable != NULL) {
									NewObjective->Variables.push_back(NewVariable);
									NewObjective->Coefficient.push_back(-atof((*ObjectiveTerms)[Count+3].data()));
								}
								if (NewVariable != NULL) {
									NewVariable = Temp->CreateMFAVariable(DRAIN_FLUX,Comp->Index,-100,0);
									AddVariable(NewVariable);
									if (ProblemLoaded) {
										LoadVariable(NewVariable->Index);
									}
									for (int j=0; j < FNumConstraints(); j++) {
										if (GetConstraint(j)->ConstraintMeaning.substr(2,GetConstraint(j)->ConstraintMeaning.length()-2).compare("mass_balance") == 0 && GetConstraint(j)->AssociatedSpecies == Temp) {
											GetConstraint(j)->Variables.push_back(NewVariable);
											GetConstraint(j)->Coefficient.push_back(1);
											if (ProblemLoaded) {
												LoadConstToSolver(GetConstraint(j)->Index);
											}
											break;
										}
									}
									NewObjective->Variables.push_back(NewVariable);
									NewObjective->Coefficient.push_back(atof((*ObjectiveTerms)[Count+3].data()));
								}
							}
						} else {
							FErrorFile() << (*ObjectiveTerms)[Count+2] << " compartment type not recognized." << endl;
							FlushErrorFile();
							return NULL;
						}
					}
					if (NewVariable == NULL) {
						FErrorFile() << (*ObjectiveTerms)[Count+1] << " has no " << (*ObjectiveTerms)[Count] << " variable type." << endl;
						FlushErrorFile();
						return NULL;
					}		
				} else {
					FErrorFile() << (*ObjectiveTerms)[Count+1] << " variable not found." << endl;
					FlushErrorFile();
					return NULL;
				}
			}
			Count += 4;
		}
	}


	AddObjective(NewObjective);
	for (int i=0; i < int(SumTypes.size()); i++) {
		AddSumObjective(SumTypes[i],QuadSum[i],true,SumCoeff[i],false);
	}
	delete ObjectiveTerms;
	return GetObjective();
}

void MFAProblem::RemoveConstraint(int ConstraintIndex, bool DeleteConstraint) {
	if (DeleteConstraint) {
		delete Constraints[ConstraintIndex];
	}
	Constraints.erase(Constraints.begin()+ConstraintIndex,Constraints.begin()+ConstraintIndex+1);
	for (int i=0; i < FNumConstraints(); i++) {
		GetConstraint(i)->Index = i;
	}
	ResetSolver();
}

void MFAProblem::RelaxConstraint(int ConstraintIndex) {
	GetConstraint(ConstraintIndex)->EqualityType = GREATER;
	GetConstraint(ConstraintIndex)->RightHandSide = -10000;
}

int MFAProblem::SaveState() {
	ProblemState* NewState = new ProblemState;
	NewState->IntegerRelation = RelaxIntegerVariables;
	NewState->Max = FMax();
	NewState->Objective = GetObjective();
	for (int i=0; i < FNumVariables(); i++) {
		NewState->Variables.push_back(GetVariable(i));
		NewState->LowerBound.push_back(GetVariable(i)->LowerBound);
		NewState->UpperBound.push_back(GetVariable(i)->UpperBound);
		NewState->Exclude.push_back(GetVariable(i)->Exclude);
	}
	for (int i=0; i < FNumConstraints(); i++) {
		NewState->Constraints.push_back(GetConstraint(i));
		NewState->RHS.push_back(GetConstraint(i)->RightHandSide);
		NewState->EqualityType.push_back(GetConstraint(i)->EqualityType);
	}
	NewState->Solutions = Solutions;
	for (int i=0; i < int(ProblemStates.size()); i++) {
		if (ProblemStates[i] == NULL) {
			ProblemStates[i] = NewState;
			return i;
		}
	}
	ProblemStates.push_back(NewState);
	return int(ProblemStates.size()-1);
}

void MFAProblem::ClearState(int InState) {
	if (InState < int(ProblemStates.size())) {
		if (ProblemStates[InState] != NULL) {
			delete ProblemStates[InState];
			ProblemStates[InState] = NULL;
		}
	}
}

void MFAProblem::LoadState(int InState, bool Bounds, bool Constraints, bool Objective, bool SaveSolutions, bool Parameters) {
	if (InState < int(ProblemStates.size())) {
		if (ProblemStates[InState] != NULL) {
			//Restoring the variable bounds in the saved state
			if (Bounds) {
				for (int i=0; i < int(ProblemStates[InState]->Variables.size()); i++) {
					ProblemStates[InState]->Variables[i]->UpperBound = ProblemStates[InState]->UpperBound[i];
					ProblemStates[InState]->Variables[i]->LowerBound = ProblemStates[InState]->LowerBound[i];
					ProblemStates[InState]->Variables[i]->Exclude = ProblemStates[InState]->Exclude[i];
				}
			}

			//Restoring the constraint parameters and the constraint number from the saved state
			if (Constraints) {
				ResetConstraintMarks(false);
				for (int i=0; i < int(ProblemStates[InState]->Constraints.size()); i++) {
					ProblemStates[InState]->Constraints[i]->RightHandSide = ProblemStates[InState]->RHS[i];
					ProblemStates[InState]->Constraints[i]->EqualityType = ProblemStates[InState]->EqualityType[i];
					ProblemStates[InState]->Constraints[i]->Mark = true;
				}
				for (int i=0; i < FNumConstraints(); i++) {
					if (!GetConstraint(i)->Mark) {
						RemoveConstraint(i);
						i--;
					}
				}
				ResetConstraintMarks(false);
			}

			//Restoring the objective of the saved state
			if (Objective) {
				if (ObjFunct != ProblemStates[InState]->Objective) {
					//delete ObjFunct;
				}
				ObjFunct = ProblemStates[InState]->Objective;
				Max = ProblemStates[InState]->Max;
			}

			//Restoring the solutions list of the saved state
			if (SaveSolutions) {
				Solutions = ProblemStates[InState]->Solutions;
			}
			
			//Storing the parameters of the saved state
			if (Parameters) {
				RelaxIntegerVariables = ProblemStates[InState]->IntegerRelation;
			}
		}
	}
}

void MFAProblem::ResetVariableMarks(bool InMark) {
	for (int i=0; i < FNumVariables(); i++) {
		GetVariable(i)->Mark = InMark;
	}
}

void MFAProblem::ResetConstraintMarks(bool InMark) {
	for (int i=0; i < FNumConstraints(); i++) {
		GetConstraint(i)->Mark = InMark;
	}
}

int MFAProblem::ApplyInputBounds(FileBounds* InBounds, Data* InData, bool ApplyToMinMax) {
	if (InBounds == NULL) {
		return 0;
	}
	
	for (int i=0; i < int(InBounds->VarName.size()); i++) {
		int Compartment = -1;
		if (InBounds->VarCompartment[i].compare("none") != 0 && InBounds->VarCompartment[i].compare("None") != 0) {
			Compartment = GetCompartment(InBounds->VarCompartment[i].data())->Index;
		}
		if (InBounds->VarType[i] == CONC || InBounds->VarType[i] == LOG_CONC || InBounds->VarType[i] == DELTAGF_ERROR || InBounds->VarType[i] == DRAIN_FLUX || InBounds->VarType[i] == FORWARD_DRAIN_FLUX || InBounds->VarType[i] == REVERSE_DRAIN_FLUX) {
			Species* Temp = InData->FindSpecies("KEGG;PALSSON;MINORG;DATABASE;NAME;ENTRY",InBounds->VarName[i].data());
			if (Temp != NULL) {
				Temp->media_concentration(InBounds->VarConc[i]);
				Temp->UpdateBounds(InBounds->VarType[i],InBounds->VarMin[i],InBounds->VarMax[i],Compartment,ApplyToMinMax);
			} else {
				FErrorFile() << "Could not find compound named: " << InBounds->VarName[i] << endl;
				FlushErrorFile();
			}
		} else if (InBounds->VarType[i] == FLUX || InBounds->VarType[i] == FORWARD_FLUX || InBounds->VarType[i] == REVERSE_FLUX || InBounds->VarType[i] == DELTAG || InBounds->VarType[i] == REACTION_DELTAG_ERROR) {
			Reaction* Temp = InData->FindReaction("KEGG;PALSSON;MINORG;DATABASE;NAME;ENTRY",InBounds->VarName[i].data());
			if (Temp != NULL) {
				Temp->UpdateBounds(InBounds->VarType[i],InBounds->VarMin[i],InBounds->VarMax[i],ApplyToMinMax);
			} else {
				FErrorFile() << "Could not find reaction named: " << InBounds->VarName[i] << endl;
				FlushErrorFile();
			}
		} else if (InBounds->VarType[i] == GENE_USE) {
			Gene* Temp = InData->FindGene("KEGG;PALSSON;MINORG;DATABASE;NAME;ENTRY",InBounds->VarName[i].data());
			if (Temp != NULL && Temp->GetMFAVar() != NULL) {
				MFAVariable* TempVar = Temp->GetMFAVar();
				TempVar->UpperBound = InBounds->VarMax[i];
				TempVar->LowerBound = InBounds->VarMin[i];
			} else {
				FErrorFile() << "Could not find reaction named: " << InBounds->VarName[i] << endl;
				FlushErrorFile();
			}
		}
	}

	return SUCCESS;
}

//This functions creates LinEquations out of the user specified constraints and adds them to the MFA problem
int MFAProblem::ApplyInputConstraints(ConstraintsToAdd* AddConstraints, Data* InData) {
	if (AddConstraints == NULL) {
		return FAIL;
	}
	
	for (int i=0; i < int(AddConstraints->RHS.size()); i++) {
		bool Pass = true;
		LinEquation* NewConstraint = InitializeLinEquation();
		NewConstraint->RightHandSide = AddConstraints->RHS[i];
		NewConstraint->ConstraintMeaning.assign("user constraint");
		NewConstraint->EqualityType = AddConstraints->EqualityType[i];
		NewConstraint->ConstraintType = LINEAR;
		for (int j=0; j < int(AddConstraints->VarCoef[i].size()); j++) {
			NewConstraint->Coefficient.push_back(AddConstraints->VarCoef[i][j]);
			//Adding the variables to the constraint
			if (AddConstraints->VarType[i][j] == CONC || AddConstraints->VarType[i][j] == LOG_CONC || AddConstraints->VarType[i][j] == DELTAGF_ERROR || AddConstraints->VarType[i][j] == DRAIN_FLUX || AddConstraints->VarType[i][j] == FORWARD_DRAIN_FLUX || AddConstraints->VarType[i][j] == REVERSE_DRAIN_FLUX) {
				Species* Temp = InData->FindSpecies("NAME;DATABASE;ENTRY",AddConstraints->VarName[i][j].data());
				if (Temp != NULL) {
					if (Temp->GetMFAVar(AddConstraints->VarType[i][j],GetCompartment(AddConstraints->VarCompartment[i][j].data())->Index) != NULL) {
						NewConstraint->Variables.push_back(Temp->GetMFAVar(AddConstraints->VarType[i][j],GetCompartment(AddConstraints->VarCompartment[i][j].data())->Index));
					} else if (AddConstraints->VarType[i][j] == DRAIN_FLUX) {	
						Pass = false;
						if (Temp->GetMFAVar(FORWARD_DRAIN_FLUX) != NULL) {
							NewConstraint->Variables.push_back(Temp->GetMFAVar(FORWARD_DRAIN_FLUX));
							Pass = true;
						}
						if (Temp->GetMFAVar(REVERSE_DRAIN_FLUX) != NULL) {
							NewConstraint->Variables.push_back(Temp->GetMFAVar(REVERSE_DRAIN_FLUX));
							if (Pass) {
								NewConstraint->Coefficient.push_back(-AddConstraints->VarCoef[i][j]);
							} else { 
								NewConstraint->Coefficient[NewConstraint->Coefficient.size()-1] = -NewConstraint->Coefficient[NewConstraint->Coefficient.size()-1];							
								Pass = true;
							}
						}
					} else if (AddConstraints->VarType[i][j] == DELTAGF_ERROR) {	
						Pass = false;
						if (Temp->GetMFAVar(DELTAGF_PERROR) != NULL) {
							NewConstraint->Variables.push_back(Temp->GetMFAVar(DELTAGF_PERROR));
							Pass = true;
						}
						if (Temp->GetMFAVar(DELTAGF_NERROR) != NULL) {
							NewConstraint->Variables.push_back(Temp->GetMFAVar(DELTAGF_NERROR));
							if (Pass) {
								NewConstraint->Coefficient.push_back(-AddConstraints->VarCoef[i][j]);
							} else { 
								NewConstraint->Coefficient[NewConstraint->Coefficient.size()-1] = -NewConstraint->Coefficient[NewConstraint->Coefficient.size()-1];							
								Pass = true;
							}
						}
					} else if (AddConstraints->VarType[i][j] == FORWARD_DRAIN_FLUX) {
						if (Temp->GetMFAVar(DRAIN_FLUX) != NULL) {
							NewConstraint->Variables.push_back(Temp->GetMFAVar(DRAIN_FLUX));
						} else { 
							Pass = false;
						}
					} else if (AddConstraints->VarType[i][j] == REVERSE_DRAIN_FLUX) {
						if (Temp->GetMFAVar(DRAIN_FLUX) != NULL) {
							NewConstraint->Variables.push_back(Temp->GetMFAVar(DRAIN_FLUX));
							NewConstraint->Coefficient[NewConstraint->Coefficient.size()-1] = -NewConstraint->Coefficient[NewConstraint->Coefficient.size()-1];				
						} else { 
							Pass = false;
						}
					} else {
						Pass = false;
					}
				} else {
					FErrorFile() << "Could not find compound named: " << AddConstraints->VarName[i][j] << endl;
					FlushErrorFile();
					Pass = false;
				}
			} else if (AddConstraints->VarType[i][j] == FLUX || AddConstraints->VarType[i][j] == FORWARD_FLUX || AddConstraints->VarType[i][j] == REVERSE_FLUX || AddConstraints->VarType[i][j] == DELTAG) {
				Reaction* Temp = InData->FindReaction("NAME;DATABASE;ENTRY",AddConstraints->VarName[i][j].data());
				if (Temp != NULL) {
					if (Temp->GetMFAVar(AddConstraints->VarType[i][j]) != NULL) {
						NewConstraint->Variables.push_back(Temp->GetMFAVar(AddConstraints->VarType[i][j]));
					} else if (AddConstraints->VarType[i][j] == FLUX) {
						Pass = false;
						if (Temp->GetMFAVar(FORWARD_FLUX) != NULL) {
							NewConstraint->Variables.push_back(Temp->GetMFAVar(FORWARD_FLUX));
							Pass = true;
						}
						if (Temp->GetMFAVar(REVERSE_FLUX) != NULL) {
							NewConstraint->Variables.push_back(Temp->GetMFAVar(REVERSE_FLUX));							
							if (Pass) {
								NewConstraint->Coefficient.push_back(-AddConstraints->VarCoef[i][j]);
							} else {
								NewConstraint->Coefficient[NewConstraint->Coefficient.size()-1] = -NewConstraint->Coefficient[NewConstraint->Coefficient.size()-1];							
								Pass = true;
							}
						}
					} else if (AddConstraints->VarType[i][j] == FORWARD_FLUX) {
						if (Temp->GetMFAVar(FLUX) != NULL) {
							NewConstraint->Variables.push_back(Temp->GetMFAVar(FLUX));
						} else { 
							Pass = false;
						}
					} else if (AddConstraints->VarType[i][j] == REVERSE_FLUX) {
						if (Temp->GetMFAVar(FLUX) != NULL) {
							NewConstraint->Variables.push_back(Temp->GetMFAVar(FLUX));
							NewConstraint->Coefficient[NewConstraint->Coefficient.size()-1] = -NewConstraint->Coefficient[NewConstraint->Coefficient.size()-1];							
						} else { 
							Pass = false;
						}
					} else {
						Pass = false;
					}
				} else {
					FErrorFile() << "Could not find reaction named: " << AddConstraints->VarName[i][j] << endl;
					FlushErrorFile();
					Pass = false;
				}
			}
		}
		if (Pass) {
			AddConstraint(NewConstraint);
		} else {
			delete NewConstraint;
			FErrorFile() << "Unable to add user constraint due to missing variable." << endl;
			FlushErrorFile();
		}
	}
	return SUCCESS;
}

int MFAProblem::ModifyInputConstraints(ConstraintsToModify* ModConstraints, Data* InData) {

	for (int i=0; i < int(ModConstraints->ConstraintName.size()); i++) {
		int index = ConstraintIndexMap[ModConstraints->ConstraintName[i].data()];

		// Only change those values that have been given by the user
		//if (ModConstraints->VarCoef[i] != NULL) {
		//	GetConstraint(index)->VarCoef = ModConstraints->VarCoef(i)
		//}
		//if (ModConstraints->VarName != NULL) {
		//	GetConstraint(index)->VarName = ModConstraints->VarName(i);
		//}
		//if (ModConstraints->VarCompartment != NULL) {
		//	GetConstraint(index)->VarCompartment = ModConstraints->VarCompartment(i);
		//}
		//if (ModConstraints->VarType != NULL) {
		//	GetConstraint(index)->VarType = ModConstraints->VarType(i);
		//}

		double test1 = ModConstraints->RHS[i];
		if (test1 != NULL){
			GetConstraint(index)->RightHandSide = ModConstraints->RHS[i];
		}
		int test2 = ModConstraints->EqualityType[i];
		if (test2 != NULL) {
			GetConstraint(index)->EqualityType = ModConstraints->EqualityType[i];
		}
		
		// Load back the modified constraint

		if (LoadConstToSolver(index) != SUCCESS) {
			FErrorFile() << ModConstraints->ConstraintName[i].data() << " cannot be modified" << endl;
		}
	}

	return SUCCESS;
}

void MFAProblem::InputSolution(OptSolutionData* InSolution) {
	for (int i=0; i < FNumVariables(); i++) {
		if (i < int(InSolution->SolutionData.size())) {
			GetVariable(i)->Value = InSolution->SolutionData[i];
		}
	}
}
//Output functions
bool MFAProblem::FMax() {
	return Max;
}

int MFAProblem::FSolver() {
	return Solver;
}

int MFAProblem::FNumConstraints() {
	return int(Constraints.size());
}

int MFAProblem::FNumSolutions() {
	return int(Solutions.size());
}

int MFAProblem::FNumVariables() {
	return int(Variables.size());
}

MFAVariable* MFAProblem::GetVariable(int InIndex) {
	return Variables[InIndex];
}

OptSolutionData* MFAProblem::GetSolution(int InIndex) {
	return Solutions[InIndex];
}

LinEquation* MFAProblem::GetConstraint(int InIndex) {
	return Constraints[InIndex];
}

LinEquation* MFAProblem::GetObjective() {
	return ObjFunct;
}

bool MFAProblem::FProblemLoaded() {
	return ProblemLoaded;
}

string MFAProblem::fluxToString() {
	string fluxes;
	map<string,double,std::less<string> > fluxData;
	for (int i=0; i < this->FNumVariables(); i++) {
		if (this->GetVariable(i)->AssociatedReaction != NULL && this->GetVariable(i)->Type == FLUX || this->GetVariable(i)->Type == FORWARD_FLUX || this->GetVariable(i)->Type == REVERSE_FLUX) {
			if (fluxData.count(this->GetVariable(i)->AssociatedReaction->GetData("DATABASE",STRING)) == 0) {
				fluxData[this->GetVariable(i)->AssociatedReaction->GetData("DATABASE",STRING)] = 0;
			}
			int sign = 1;
			if (this->GetVariable(i)->Type == REVERSE_FLUX) {
				sign = -1;
			}
			fluxData[this->GetVariable(i)->AssociatedReaction->GetData("DATABASE",STRING)] += sign*(this->GetVariable(i)->Value);
		} else if (this->GetVariable(i)->AssociatedSpecies != NULL && this->GetVariable(i)->Type == DRAIN_FLUX || this->GetVariable(i)->Type == FORWARD_DRAIN_FLUX || this->GetVariable(i)->Type == REVERSE_DRAIN_FLUX) {
			if (fluxData.count(this->GetVariable(i)->AssociatedSpecies->GetData("DATABASE",STRING)) == 0) {
				fluxData[this->GetVariable(i)->AssociatedSpecies->GetData("DATABASE",STRING)] = 0;
			}
			int sign = 1;
			if (this->GetVariable(i)->Type == REVERSE_DRAIN_FLUX) {
				sign = -1;
			}
			fluxData[this->GetVariable(i)->AssociatedSpecies->GetData("DATABASE",STRING)] += sign*(this->GetVariable(i)->Value);
		}
	}
	for (map<string,double,std::less<string> >::iterator IT = fluxData.begin(); IT != fluxData.end(); IT++) {
		if (fabs(IT->second) > 1e-7) {
			if (fluxes.length() > 0) {
				fluxes.append(";");
			}
			fluxes.append(IT->first+":"+dtoa(IT->second));
		}
	}
	return fluxes;
}

//Solver interaction
int MFAProblem::LoadSolver(bool PrintFromSolver) {
	int Status = FAIL;
	DetermineProbType();
	Status = GlobalInitializeSolver(Solver);	
	ProblemLoaded = false;
	LoadedRelaxation = RelaxIntegerVariables;
	if (Status != SUCCESS) {
		FErrorFile() << "Failed to load solver because solver could not be cleared." << endl;
		FlushErrorFile();
		return FAIL;
	}
	if (LoadAllVariables() != SUCCESS) {
		FErrorFile() << "Failed to load solver because variables could not be loaded." << endl;
		FlushErrorFile();
		return FAIL;	
	}
	if (LoadAllConstToSolver() != SUCCESS) {
		FErrorFile() << "Failed to load solver because constraints could not be loaded." << endl;
		FlushErrorFile();
		return FAIL;	
	}
	if (LoadObjective() != SUCCESS) {
		FErrorFile() << "Failed to load solver because objective could not be loaded." << endl;
		FlushErrorFile();
		return FAIL;	
	}
	ProblemLoaded = true;
	if (PrintFromSolver) {
		WriteLPFile();
	}
	return SUCCESS;
}	

OptSolutionData* MFAProblem::RunSolver(bool SaveSolution, bool InInputSolution,bool WriteProblem) {
	DetermineProbType();
	
	if (WriteProblem) {
		PrintVariableKey();
		WriteLPFile();
	}
	OptSolutionData* CurrentSolution = NULL;

	CurrentSolution = GlobalRunSolver(Solver,ProbType);
	
	if (SaveSolution && CurrentSolution != NULL) {
		if (CurrentSolution->Status == SUCCESS) {
			Solutions.push_back(CurrentSolution);
		}
	}
	if (InInputSolution && CurrentSolution != NULL) {
		if (CurrentSolution->Status == SUCCESS) {
			InputSolution(CurrentSolution);
		}
	}

	return CurrentSolution;
}

int MFAProblem::LoadVariable(int InIndex) {
	GetVariable(InIndex)->Index = InIndex;
	int Status;
	if (GetParameter("Always relax integer variables").compare("1") == 1) {
		Status = GlobalLoadVariable(Solver,GetVariable(InIndex), true,UseTightBounds);
	} else {
		Status = GlobalLoadVariable(Solver,GetVariable(InIndex), RelaxIntegerVariables,UseTightBounds);
	}
	if (Status == SUCCESS) {
		GetVariable(InIndex)->Loaded = true;
		GetVariable(InIndex)->LoadedLowerBound = GetVariable(InIndex)->LowerBound;
		GetVariable(InIndex)->LoadedUpperBound = GetVariable(InIndex)->UpperBound;
	}
	return Status;
}

int MFAProblem::ResetSolver() {
	if (ProblemLoaded) {
		ProblemLoaded = false;
		LoadedRelaxation = RelaxIntegerVariables;

		for (int i=0; i < FNumVariables(); i++) {
			GetVariable(i)->Loaded = false;
			GetVariable(i)->LoadedLowerBound = FLAG;
			GetVariable(i)->LoadedUpperBound = FLAG;
		}

		for (int i=0; i < FNumConstraints(); i++) {
			GetConstraint(i)->Loaded = false;
			GetConstraint(i)->LoadedEqualityType = int(FLAG);
			GetConstraint(i)->LoadedRightHandSide = FLAG;
		}

		ObjFunct->Loaded = false;

		return GlobalResetSolver(Solver);
	}
	return SUCCESS;
}

int MFAProblem::UpdateLoadSolver() {
	if (ProblemLoaded) {
		if (LoadedRelaxation != RelaxIntegerVariables) {
			ResetSolver();
		}
	}

	for (int i=0; i < FNumVariables(); i++) {
		if (!GetVariable(i)->Loaded || GetVariable(i)->UpperBound != GetVariable(i)->LoadedUpperBound || GetVariable(i)->LowerBound != GetVariable(i)->LoadedLowerBound) {
			if (LoadVariable(i) != SUCCESS) {
				FErrorFile() << "Failed to load solver because variables could not be loaded." << endl;
				FlushErrorFile();
				return FAIL;	
			};
		}
	}
	
	for (int i=0; i < FNumConstraints(); i++) {
		if (!GetConstraint(i)->Loaded || GetConstraint(i)->RightHandSide != GetConstraint(i)->LoadedRightHandSide || GetConstraint(i)->EqualityType != GetConstraint(i)->LoadedEqualityType) {
			if (LoadConstToSolver(i) != SUCCESS) {
				FErrorFile() << "Failed to load solver because constraints could not be loaded." << endl;
				FlushErrorFile();
				return FAIL;
			}
		}
	}

	if (!ObjFunct->Loaded) {
		if (LoadObjective() != SUCCESS) {
			FErrorFile() << "Failed to load solver because objective could not be loaded." << endl;
			FlushErrorFile();
			return FAIL;	
		}
	}

	return SUCCESS;
}

SavedBounds* MFAProblem::saveBounds() {
	SavedBounds* newBounds = new SavedBounds;
	for (int i=0; i < FNumVariables(); i++) {
		newBounds->variables.push_back(this->GetVariable(i));
		newBounds->lowerBounds.push_back(this->GetVariable(i)->LowerBound);
		newBounds->upperBounds.push_back(this->GetVariable(i)->UpperBound);
	}
	return newBounds;
}

int MFAProblem::loadBounds(SavedBounds* inBounds,bool loadProblem) {
	for (int i=0; i < int(inBounds->variables.size()); i++) {
		if (inBounds->variables[i]->LowerBound != inBounds->lowerBounds[i] || inBounds->variables[i]->UpperBound != inBounds->upperBounds[i]) {
			inBounds->variables[i]->LowerBound = inBounds->lowerBounds[i];
			inBounds->variables[i]->UpperBound = inBounds->upperBounds[i];
			if (loadProblem) {
				this->LoadVariable(inBounds->variables[i]->Index);
			}
		}
	}
	return SUCCESS;
}

int MFAProblem::loadChangedBoundsIntoSolver(SavedBounds* inBounds) {
	for (int i=0; i < int(inBounds->variables.size()); i++) {
		if (inBounds->variables[i]->LowerBound != inBounds->lowerBounds[i] || inBounds->variables[i]->UpperBound != inBounds->upperBounds[i]) {
			this->LoadVariable(inBounds->variables[i]->Index);
		}
	}
	return SUCCESS;
}

int MFAProblem::loadMedia(string media, Data* inData,bool loadIntoSolver) {
	vector<MFAVariable*> mediaVariables;
	vector<double> upperBounds;
	vector<double> lowerBounds;
	for (int i=0; i < this->FNumVariables(); i++) {
		if (this->GetVariable(i)->Compartment == GetCompartment("e")->Index) {
			if (this->GetVariable(i)->Type == DRAIN_FLUX || this->GetVariable(i)->Type == FORWARD_DRAIN_FLUX) {
				mediaVariables.push_back(this->GetVariable(i));
				upperBounds.push_back(this->GetVariable(i)->UpperBound);
				lowerBounds.push_back(this->GetVariable(i)->LowerBound);
				this->GetVariable(i)->UpperBound = 0;
			}
		}
	}
	if (media.compare("Complete") == 0) {
		for (int i=0; i < int(mediaVariables.size()); i++) {
			mediaVariables[i]->UpperBound = 100;
		}
	} else {
		FileBounds* mediaObj = ReadBounds((media+".txt").data());
		if (mediaObj == NULL) {
			return FAIL;
		}
		ApplyInputBounds(mediaObj,inData);
	}
	if (loadIntoSolver) {
		for (int i=0; i < int(mediaVariables.size()); i++) {
			if (upperBounds[i] != mediaVariables[i]->UpperBound || lowerBounds[i] != mediaVariables[i]->LowerBound) {
				LoadVariable(mediaVariables[i]->Index);
			}
		}
	}
	return SUCCESS;
}

int MFAProblem::clearOldMedia(OptimizationParameter* InParameters) {
	for (int i=0; i < FNumVariables(); i++) {
		if (GetVariable(i)->Compartment ==  GetCompartment("e")->Index) {
			if (GetVariable(i)->Type == DRAIN_FLUX) {
				if (GetVariable(i)->UpperBound != InParameters->MaxDrainFlux || GetVariable(i)->LowerBound != InParameters->MinDrainFlux) {
					GetVariable(i)->UpperBound = InParameters->MaxDrainFlux;
					GetVariable(i)->LowerBound = InParameters->MinDrainFlux;
					LoadVariable(i);
				}
			} else if (GetVariable(i)->Type == FORWARD_DRAIN_FLUX) {
				bool Change = false;
				if (InParameters->MaxDrainFlux > 0 && GetVariable(i)->UpperBound != InParameters->MaxDrainFlux) {
					GetVariable(i)->UpperBound = InParameters->MaxDrainFlux;
					Change = true;
				} else if (InParameters->MaxDrainFlux <= 0 && GetVariable(i)->UpperBound != 0) {
					GetVariable(i)->UpperBound = 0;
					Change = true;
				}
				if (InParameters->MinDrainFlux > 0 && GetVariable(i)->LowerBound != InParameters->MinDrainFlux) {
					GetVariable(i)->LowerBound = InParameters->MinDrainFlux;
					Change = true;
				} else if (InParameters->MinDrainFlux <= 0 && GetVariable(i)->LowerBound != 0) {
					GetVariable(i)->LowerBound = 0;
					Change = true;
				}
				if (Change) {
					LoadVariable(i);
				}
			} else if (GetVariable(i)->Type == REVERSE_DRAIN_FLUX) {
				bool Change = false;
				if (InParameters->MaxDrainFlux > 0 && GetVariable(i)->LowerBound != 0) {
					GetVariable(i)->LowerBound = 0;
					Change = true;
				} else if (InParameters->MaxDrainFlux <= 0 && GetVariable(i)->LowerBound != -InParameters->MaxDrainFlux) {
					GetVariable(i)->LowerBound = -InParameters->MaxDrainFlux;
					Change = true;
				}
				if (InParameters->MinDrainFlux > 0 && GetVariable(i)->UpperBound != 0) {
					GetVariable(i)->UpperBound = 0;
					Change = true;
				} else if (InParameters->MinDrainFlux <= 0 && GetVariable(i)->UpperBound != -InParameters->MinDrainFlux) {
					GetVariable(i)->UpperBound = -InParameters->MinDrainFlux;
					Change = true;
				}
				if (Change) {
					LoadVariable(i);
				}
			} else if (GetParameter("Base compound regulation on media files").compare("1") == 0 && (GetVariable(i)->Type == FORWARD_DRAIN_USE || GetVariable(i)->Type == REVERSE_DRAIN_USE || GetVariable(i)->Type == DRAIN_USE)) {
				GetVariable(i)->LowerBound = 0;
				GetVariable(i)->UpperBound = 1;
				LoadVariable(i);
			}
		}
	}
	return SUCCESS;
}

int MFAProblem::LoadAllVariables() {
	for (int i=0; i < FNumVariables(); i++) {
		if (LoadVariable(i) != SUCCESS) {
			return FAIL;
		}
	}
	return SUCCESS;
}

int MFAProblem::LoadObjective() {
	int Status = GlobalLoadObjective(Solver,GetObjective(), FMax());
	if (Status == SUCCESS) {
		GetObjective()->Loaded = true;
	}
	return Status;
}

int MFAProblem::LoadConstToSolver(int InIndex) {
	GetConstraint(InIndex)->Index = InIndex;
	int Status = FAIL;

	for (int i=0; i < int(GetConstraint(InIndex)->Variables.size()-1); i++) {
		for (int j=i+1; j < int(GetConstraint(InIndex)->Variables.size()-1); j++) {
			if (GetConstraint(InIndex)->Variables[i] == GetConstraint(InIndex)->Variables[j]) {
				GetConstraint(InIndex)->Variables.erase(GetConstraint(InIndex)->Variables.begin()+j,GetConstraint(InIndex)->Variables.begin()+j+1);
				GetConstraint(InIndex)->Coefficient[i] += GetConstraint(InIndex)->Coefficient[j];
				GetConstraint(InIndex)->Coefficient.erase(GetConstraint(InIndex)->Coefficient.begin()+j,GetConstraint(InIndex)->Coefficient.begin()+j+1);
				j--;
			}
		}
	}
	
	// if we are checking the feasibilities of the chemical potential we load in relaxed versions
	if (GetParameter("Check potential constraints feasibility").compare("1") == 0) {
		if (GetConstraint(InIndex)->ConstraintMeaning.compare("chemical potential constraint") == 0){
			if (GetConstraint(InIndex)->Loaded == false) {
				GetConstraint(InIndex)->LoadedEqualityType = LESS;
				GetConstraint(InIndex)->LoadedRightHandSide = FLAG;
				Status = GlobalAddConstraint(Solver, GetConstraint(InIndex));
			} else {
				GetConstraint(InIndex)->EqualityType = EQUAL;
				GetConstraint(InIndex)->LoadedEqualityType = EQUAL;
				GetConstraint(InIndex)->LoadedRightHandSide = GetConstraint(InIndex)->RightHandSide;
				Status = GlobalAddConstraint(Solver, GetConstraint(InIndex));
			}
		} else {
			Status = GlobalAddConstraint(Solver, GetConstraint(InIndex));
			if (Status == SUCCESS) {
				GetConstraint(InIndex)->Loaded = true;
				GetConstraint(InIndex)->LoadedEqualityType = GetConstraint(InIndex)->EqualityType;
				GetConstraint(InIndex)->LoadedRightHandSide = GetConstraint(InIndex)->RightHandSide;
			}
		}
	} else {
		// we do not add the potential energy constraints for compounds which we have errors in estimating the deltaGFs
		// these compounds usually have deltaGF energies indicated at 1e07 and also constraints that we have specified in the list of
		// user constraints to exclude
		if ((GetConstraint(InIndex)->ConstraintMeaning.compare("chemical potential constraint") == 0) && (GetConstraint(InIndex)->RightHandSide > 0.9*FLAG)){
			GetConstraint(InIndex)->Loaded = false;
			GetConstraint(InIndex)->LoadedEqualityType = LESS;
			GetConstraint(InIndex)->LoadedRightHandSide = FLAG;
			Status = GlobalAddConstraint(Solver, GetConstraint(InIndex));
		} else {
			Status = GlobalAddConstraint(Solver, GetConstraint(InIndex));
		}
		if (Status == SUCCESS) {
			GetConstraint(InIndex)->Loaded = true;
			GetConstraint(InIndex)->LoadedEqualityType = GetConstraint(InIndex)->EqualityType;
			GetConstraint(InIndex)->LoadedRightHandSide = GetConstraint(InIndex)->RightHandSide;
		}
	}

	return Status;
}

int MFAProblem::LoadAllConstToSolver() {
	for (int i=0; i < FNumConstraints(); i++) {
		if (LoadConstToSolver(i) != SUCCESS) {
			return FAIL;	
		} else {
			ConstraintIndexMap[GetConstraintName(GetConstraint(i)).data()] = i;
		}
	}
	// FlushErrorFile();
	return SUCCESS;
}

//Analysis functions
//This function calculates the average total concentration, average ionic strength, average std dev, and average average concentration for a set of solutions 
map<int , vector<double> , std::less<int> >* MFAProblem::CalcConcMeanIonicStrength() { //Average total, Average ionic strength, Average Std dev, Average average conc
	map<int , vector<double> , std::less<int> >* Result = new map<int , vector<double> , std::less<int> >;
	map<int , int , std::less<int> > TotalPoints;
	
	for (int i=0; i < FNumSolutions(); i++) {
		map<int , vector<double> , std::less<int> >::iterator MapITTwo = GetSolution(i)->ConcentrationStats.begin();
		for (int i=0; i < int(TotalPoints.size()); i++) {
			vector<double>& Temp = (*Result)[MapITTwo->first];
			if (Temp.size() == 0) {
				TotalPoints[MapITTwo->first] = 0;
				Temp.push_back(0);
				Temp.push_back(0);
				Temp.push_back(0);
				Temp.push_back(0);
			}
			TotalPoints[MapITTwo->first]++;
			Temp[0] += MapITTwo->second[0];
			Temp[1] += MapITTwo->second[1];
			Temp[2] += MapITTwo->second[2];
			Temp[3] += MapITTwo->second[3];
			MapITTwo++;
		}
	}
	
	map<int , int , std::less<int> >::iterator MapITOne = TotalPoints.begin();
	map<int , vector<double> , std::less<int> >::iterator MapITTwo = Result->begin();
	for (int i=0; i < int(TotalPoints.size()); i++) {
		MapITTwo->second[0] = MapITTwo->second[0]/MapITOne->second;
		MapITTwo->second[1] = MapITTwo->second[1]/MapITOne->second;
		MapITTwo->second[2] = MapITTwo->second[2]/MapITOne->second;
		MapITTwo->second[3] = MapITTwo->second[3]/MapITOne->second;
		MapITOne++;
		MapITTwo++;
	}

	return Result;
}

//This function calculates the total concentration, ionic strength, average concentration, and the standard deviation of the concentration for the input solution
void MFAProblem::ProcessSolution(OptSolutionData* InSolution) {
	map<int , int , std::less<int> > TotalPoints;

	for (int j=0; j < FNumVariables(); j++) {
		GetVariable(j)->Value = InSolution->SolutionData[j];
		if (GetVariable(j)->Type == CONC) {
			vector<double>& Temp = InSolution->ConcentrationStats[GetVariable(j)->Compartment];
			if (Temp.size() == 0) {
				TotalPoints[GetVariable(j)->Compartment] = 0;
				Temp.push_back(0);
				Temp.push_back(0);
				Temp.push_back(0);
				Temp.push_back(0);
			}
			TotalPoints[GetVariable(j)->Compartment]++;
			Temp[0] += GetVariable(j)->Value;
			Temp[1] += 0.5*GetVariable(j)->Value*GetVariable(j)->AssociatedSpecies->FCharge()*GetVariable(j)->AssociatedSpecies->FCharge();
		} else if (GetVariable(j)->Type == LOG_CONC) {
			vector<double>& Temp = InSolution->ConcentrationStats[GetVariable(j)->Compartment];
			if (Temp.size() == 0) {
				TotalPoints[GetVariable(j)->Compartment] = 0;
				Temp.push_back(0);
				Temp.push_back(0);
				Temp.push_back(0);
				Temp.push_back(0);
			}
			TotalPoints[GetVariable(j)->Compartment]++;
			Temp[0] += exp(GetVariable(j)->Value);
			Temp[1] += 0.5*exp(GetVariable(j)->Value)*GetVariable(j)->AssociatedSpecies->FCharge()*GetVariable(j)->AssociatedSpecies->FCharge();
		}	
	}

	map<int , int , std::less<int> >::iterator MapITOne = TotalPoints.begin();
	map<int , vector<double> , std::less<int> >::iterator MapITTwo = InSolution->ConcentrationStats.begin();
	for (int i=0; i < int(TotalPoints.size()); i++) {
		MapITTwo->second[2] = MapITTwo->second[1]/MapITOne->second;
		MapITOne++;
		MapITTwo++;
	}

	for (int j=0; j < FNumVariables(); j++) {
		if (GetVariable(j)->Type == CONC) {
			vector<double>& Temp = InSolution->ConcentrationStats[GetVariable(j)->Compartment];
			Temp[3] += (GetVariable(j)->Value-Temp[0])*(GetVariable(j)->Value-Temp[0]);
		} else if (GetVariable(j)->Type == LOG_CONC) {
			vector<double>& Temp = InSolution->ConcentrationStats[GetVariable(j)->Compartment];
			Temp[3] += (exp(GetVariable(j)->Value)-Temp[0])*(exp(GetVariable(j)->Value)-Temp[0]);
		}	
	}

	MapITOne = TotalPoints.begin();
	MapITTwo = InSolution->ConcentrationStats.begin();
	for (int i=0; i < int(TotalPoints.size()); i++) {
		MapITTwo->second[3] = MapITTwo->second[3]/MapITOne->second;
		MapITTwo->second[3] = pow(MapITTwo->second[3],0.5);
		MapITOne++;
		MapITTwo++;
	}
}

//This function determines the tight bounds on the variables specified in the "tight bounds search variables" parameter and stores the results in file and in the variable data structure
int MFAProblem::FindTightBounds(Data* InData,OptimizationParameter*& InParameters, bool SaveSolution, bool UseSpecifiedSearchTypes) {
	bool First = true;
	int Status = SUCCESS;

	if (FNumVariables() == 0) {
		if (BuildMFAProblem(InData,InParameters) != SUCCESS) {
			FErrorFile() << "Failed to build optimization problem required to determine tight bounds." << endl;
			FlushErrorFile();
			return FAIL;	
		}
	}

	vector<int> SearchTypes;
	if (UseSpecifiedSearchTypes) {
		vector<string>* Strings = StringToStrings(GetParameter("tight bounds search variables"), ";", true);
		for (int i=0; i < int(Strings->size()); i++) {
			SearchTypes.push_back(ConvertVariableType((*Strings)[i]));
		}
		delete Strings;
		ResetVariableMarks(false);
	}
	
	ClearObjective(false);
	ObjFunct = InitializeLinEquation();
	ObjFunct->Coefficient.push_back(1);
	ObjFunct->Variables.push_back(NULL);
	Status = ResetSolver();
	if (Status != SUCCESS) {
		return Status;
	}

	int InitialSolutions = FNumSolutions();
	for (int i=0; i < FNumVariables(); i++) {
		if (UseSpecifiedSearchTypes) {
			for (int j=0; j < int(SearchTypes.size()); j++) {
				if (SearchTypes[j] == GetVariable(i)->Type) {
					GetVariable(i)->Mark = true;
					break;
				}
			}
		}
		MFAVariable* linkedvar = NULL;
		if (GetVariable(i)->Mark && GetVariable(i)->Type == FORWARD_FLUX) {
			for (int j=0; j < FNumVariables(); j++) {
				if (GetVariable(i)->AssociatedReaction == GetVariable(j)->AssociatedReaction && GetVariable(j)->Type == REVERSE_FLUX) {
					linkedvar = GetVariable(j);
					break;
				}
			}
		}
		if (GetVariable(i)->Mark && GetVariable(i)->Type == REVERSE_FLUX) {
			for (int j=0; j < FNumVariables(); j++) {
				if (GetVariable(i)->AssociatedReaction == GetVariable(j)->AssociatedReaction && GetVariable(j)->Type == FORWARD_FLUX) {
					GetVariable(i)->Mark = false;
					break;
				}
			}
		}

		if (GetVariable(i)->Mark) {
			OptSolutionData* NewSolution;
			SetMin();
			ObjFunct->Variables[0] = GetVariable(i);
			if (linkedvar != NULL) {
				ObjFunct->Variables.push_back(linkedvar);
				ObjFunct->Coefficient.push_back(-1);
			}
			if (First) {
				if (LoadSolver() != SUCCESS) {
					return FAIL;	
				}
				NewSolution = RunSolver(SaveSolution,false,false);
				First = false;
			} else {
				LoadObjective();
				NewSolution = RunSolver(SaveSolution,false,false);
			}
			if (NewSolution != NULL && NewSolution->Status == SUCCESS) {
				GetVariable(i)->Min = NewSolution->Objective;
				if (!SaveSolution) {
					delete NewSolution;
				}
			} else {
				GetVariable(i)->Min = FLAG;
			}
			if (linkedvar != NULL) {
				if (GetVariable(i)->Min < 0) {
					linkedvar->Min = 0;
					linkedvar->Max = -GetVariable(i)->Min;
					GetVariable(i)->Min = 0;
				} else {
					linkedvar->Min = 0;
					linkedvar->Max = 0;
				}
			}

			SetMax();
			LoadObjective();
			NewSolution = RunSolver(SaveSolution,false,false);
			if (NewSolution != NULL && NewSolution->Status == SUCCESS) {
				GetVariable(i)->Max = NewSolution->Objective;
				if (!SaveSolution) {
					delete NewSolution;
				}
			} else {
				GetVariable(i)->Max = FLAG;
			}
			if (linkedvar != NULL) {
				if (GetVariable(i)->Max < 0) {
					linkedvar->Min = -GetVariable(i)->Max;
					GetVariable(i)->Max = 0;
					GetVariable(i)->Min = 0;
				} else {
					linkedvar->Min = 0;
				}
				ObjFunct->Variables.pop_back();
				ObjFunct->Coefficient.pop_back();
				cout << GetVariable(i)->Min << " " << GetVariable(i)->Max << " " << linkedvar->Min << " " << linkedvar->Max << endl;
			}
		}
	}
	
	if (SaveSolution) {
		PrintSolutions(InitialSolutions,-1,true);
		ClearSolutions(InitialSolutions,-1,true);
	}

	SaveTightBounds(); 

	return Status;
}

int MFAProblem::CalculateFluxSensitivity(Data* InData,vector<MFAVariable*> variables,double objective) {
	//Checking that the FBA problem has been loaded into the FBA datastructure
	if (this->FNumVariables() == 0) {
		cerr << "MFAProblem->CalculateFluxSensitivity(): problem must be initialized before calling CalculateFluxSensitivity()" << endl;
		return FAIL;
	}	
	//Iterating over all input variables to calculate the shadow price for each variable at its current value
	double fractionPerturbation = 0.01;
	vector<double> shadowPrices(variables.size());
	vector<double> perturbationList(variables.size());
	for (int i=0; i < int(variables.size()); i++) {
		MFAVariable* currentVariable = variables[i];
		bool performPerturbation = true;
		double perturbation;
		//Running FVA on variable
		currentVariable->Max = this->optimizeVariable(currentVariable,true);
		currentVariable->Min = this->optimizeVariable(currentVariable,false);
		//Checking that the feasible range for the variable is large enough for perturbation
		if ((currentVariable->Max-currentVariable->Min) < SHADOW_ZERO_TOLERANCE) {
			//Checking if the variables value is zero
			if (fabs(currentVariable->Value) < SHADOW_ZERO_TOLERANCE) {
				//If the value is fixed at zero, we set the shadow price to zero
				shadowPrices[i] = 0;
				perturbationList[i] = FLAG;
			} else {
				//If the value is not zero but the value is fixed, we flag the shadowprice
				shadowPrices[i] = FLAG;
				perturbationList[i] = FLAG;
			}
			performPerturbation = false;
		}
		//Checking if we should still perform the perturbation
		if (performPerturbation) {
			//Checking if the reference value is zero
			perturbation = fractionPerturbation*(currentVariable->Max-currentVariable->Min);
			if (perturbation > SHADOW_MAX_PERTURBATION) {
				perturbation = SHADOW_MAX_PERTURBATION;
			}
			//Determining which direction the perturbation will be in
			if ((currentVariable->Value - perturbation) < currentVariable->Min) {
				if ((currentVariable->Value + perturbation) > currentVariable->Max) {
					if (currentVariable->Max-currentVariable->Value > currentVariable->Value-currentVariable->Min) {
						perturbation = currentVariable->Max-currentVariable->Value;
					} else {
						perturbation = currentVariable->Min-currentVariable->Value;
					}
				}
			} else {
				perturbation = -1*perturbation;
			}
			//Storing the current bounds of the variable
			double originalUpper = currentVariable->UpperBound;
			double originalLower = currentVariable->LowerBound;
			//Setting the bounds equal to the perturbed values
			currentVariable->UpperBound = currentVariable->Value + perturbation;
			currentVariable->LowerBound = currentVariable->Value + perturbation;
			//Loading the new bounds into the solver
			this->LoadVariable(currentVariable->Index);
			//Rerunning the optimization
			OptSolutionData* newSolution = this->RunSolver(false,false,true);
			perturbationList[i] = perturbation;
			if (newSolution != NULL && newSolution->Status == SUCCESS) {
				shadowPrices[i] = (newSolution->Objective-objective)/perturbation;
			} else {
				shadowPrices[i] = FLAG;
			}
			//Restoring variable to original bounds
			currentVariable->UpperBound = originalUpper;
			currentVariable->LowerBound = originalLower;
			//Loading original bounds into the solver
			this->LoadVariable(currentVariable->Index);
		}
	}
	//Setting the output filename
	string filename(GetParameter("MFA output path") + "Sensitivities.tbl");
	//Printing the reaction results to file
	ostringstream outputStream;
	outputStream << "DATABASE ID;Type;Value;Perturbation;Sensitivity;Minimum;Maximum" << endl;
	for (int i=0; i < int(variables.size()); i++) {
		outputStream << variables[i]->Name << ";" << ConvertVariableType(variables[i]->Type) << ";" << variables[i]->Value << ";" << perturbationList[i] << ";" << shadowPrices[i] << ";" << variables[i]->Min << ";" << variables[i]->Max << endl;
	}
	if (printOutput(filename,outputStream.str()) != SUCCESS) {
		cerr << "MFAProblem->CalculateFluxSensitivity(): failed to print output to " << filename << endl;
		return FAIL;
	}
	return SUCCESS;
}

double MFAProblem::optimizeVariable(MFAVariable* currentVariable,bool maximize) {
	//Saving the current objective
	LinEquation* currentObj = this->ObjFunct;
	bool currentSense = this->Max;
	//Creating the new objective that optimizes variable
	this->Max = maximize;
	this->ObjFunct = InitializeLinEquation();
	ObjFunct->Variables.push_back(currentVariable);
	ObjFunct->Coefficient.push_back(1);
	this->LoadObjective();
	OptSolutionData* solution = this->RunSolver(false,false,false);
	//Restoring the current objective
	this->Max = currentSense;
	this->ObjFunct = currentObj;
	if (currentObj != NULL && currentObj->Variables.size() > 0) {
		this->LoadObjective();
	}
	//Checking if the solution was valid
	if (solution == NULL || solution->Status != SUCCESS) {
		cerr << "MFAProblem->maximizeVariable(): variable " << currentVariable->Name << " could not be maximized" << endl;
		return FLAG;
	}
	return solution->Objective;
}

//This function extends standard flux balance analysis by simulating combinatorial deletions up to the maximum specified by the "maxDeletions" argument
int MFAProblem::CombinatorialKO(int maxDeletions,Data* InData,bool reactions = false) {
	//Checking that a problem exists
	if (FNumVariables() == 0) {
		return FAIL;
	}
	//Getting the wildtype growth
	this->RelaxIntegerVariables = true;
	if (this->Solver != CPLEX) {
		this->Solver = GLPK;
	}
	this->LoadSolver();
	OptSolutionData* newSolution = RunSolver(false,false,false);
	if (newSolution == NULL || newSolution->Status != SUCCESS || newSolution->Objective < MFA_ZERO_TOLERANCE) {
		return FAIL;		
	}
	string essentialList;
	double wildType = newSolution->Objective;
	DataNode* delTree = new DataNode;
	delTree->data = -1;
	//Saving growth reduction and KO
	ofstream Output;
	string outputFilename = FOutputFilepath()+"MFAOutput/CombinationKO.txt";
	if (reactions) {
		outputFilename = FOutputFilepath()+"MFAOutput/ReactionCombinationKO.txt";
	}
	if (!OpenOutput(Output,outputFilename)) {
		return FAIL;
	}
	Output << "Wildtype\t" << wildType << endl;
	//Iterating up to the maximum number of deletions
	int numEntities = InData->FNumGenes();
	if (reactions) {
		numEntities = InData->FNumReactions();
	}
	for (int i=0; i < maxDeletions; i++) {
		//Storing the indecies of the current set of deleted genes
		vector<int> counter(i+1);
		for (int j=0; j < i+1; j++) {
			counter[j] = j;
		}
		bool complete = false;
		while (!complete) {
			//Checking that the current selection involves no lethal combinations
			double subType = wildType;
			//Changing the start point
			for (int j=0; j < int(counter.size());j++) {
				if (delTree->children.count(counter[j]) > 0) {
					vector<DataNode*> currNodes(1,delTree->children[counter[j]]);
					for (int k=j+1; k < int(counter.size());k++) {
						for (int m=0; m < int(currNodes.size()); m++) {
							if (currNodes[m]->children.count(counter[k]) > 0) {
								currNodes.push_back(currNodes[m]->children[counter[k]]);
							}
						}
					}
					for (int k=0; k < int(currNodes.size()); k++) {
						if (currNodes[k]->data >= 0 && currNodes[k]->data < subType) {
							subType = currNodes[k]->data;
						}
					}
				}
				if (subType == 0) {
					break;
				}
			}
			//Only attempting deletion if the smallest sub-phenotype is still greater than zero
			if (subType > 0) {
				//Setting all reaction marks to false
				for (int j=0; j < InData->FNumReactions(); j++) {
					InData->GetReaction(j)->SetMark(false);
				}
				//Marking reactions to be knocked out in the simulation
				if (reactions) {
					for (int j=0; j < i+1; j++) {
						InData->GetReaction(counter[j])->SetMark(true);
					}
				} else {
					//Setting the specified genes as knocked out
					for (int j=0; j < i+1; j++) {
						InData->GetGene(counter[j])->SetMark(true);
					}
					//Checking which reactions are knocked out with the genes
					for (int j=0; j < InData->FNumReactions(); j++) {
						if (InData->GetReaction(j)->CheckForKO()) {
							InData->GetReaction(j)->SetMark(true);
						}
					}
				}
				//Saving the bounds of deleted reactions then setting bounds to zero
				vector<double> oldMin;
				vector<double> oldMax;
				vector<MFAVariable*> entities;
				for (int j=0; j < this->FNumVariables(); j++) {
					if (this->GetVariable(j)->Type == FLUX || this->GetVariable(j)->Type == FORWARD_FLUX  || this->GetVariable(j)->Type == REVERSE_FLUX) {
						if (this->GetVariable(j)->AssociatedReaction != NULL && this->GetVariable(j)->AssociatedReaction->FMark()) {
							entities.push_back(this->GetVariable(j));
							oldMin.push_back(this->GetVariable(j)->LowerBound);
							this->GetVariable(j)->LowerBound = 0;
							oldMax.push_back(this->GetVariable(j)->UpperBound);
							this->GetVariable(j)->UpperBound = 0;
							this->LoadVariable(this->GetVariable(j)->Index);
						}
					}
				}
				//Running optimization
				OptSolutionData* newSolution = RunSolver(false,false,false);
				double obj = 0;
				if (newSolution != NULL && newSolution->Status == SUCCESS && newSolution->Objective > MFA_ZERO_TOLERANCE) {
					obj = newSolution->Objective;
				}
				delete newSolution;
				if ((obj/subType) < 0.95) {
					DataNode* tempNode = delTree;
					for (int j=0; j < int(counter.size()); j++) {
						if (tempNode->children[counter[j]] == NULL) {
							tempNode->children[counter[j]] = new DataNode;
							tempNode->children[counter[j]]->data = -1;
						}
						tempNode = tempNode->children[counter[j]];
						if (j > 0) {
							Output << ",";
						}
						if (reactions) {
							Output << InData->GetReaction(counter[j])->GetData("DATABASE",STRING);
						} else {
							if (i == 0 && obj == 0) {
								if (essentialList.length() > 0) {
									essentialList.append(";");
								}
								essentialList.append(InData->GetGene(counter[j])->GetData("DATABASE",STRING));
								InData->GetGene(counter[j])->SetData("CLASS","Essential",STRING);
							}
							Output << InData->GetGene(counter[j])->GetData("DATABASE",STRING);
						}
					}
					tempNode->data = obj;
					Output << "\t" << obj << endl;
				}
				if (!reactions) {
					//Restoring the specified genes
					for (int j=0; j < i+1; j++) {
						InData->GetGene(counter[j])->SetMark(false);
					}
				}
				//Restoring the original reaction bounds
				for (int j=0; j < int(entities.size()); j++) {
					entities[j]->UpperBound = oldMax[j];
					entities[j]->LowerBound = oldMin[j];
					this->LoadVariable(entities[j]->Index);
				}
			}
			//Iterating the counter
			int index = int(counter.size()-1);
			while(index >= 0 && (counter[index]+1) >= (numEntities-int(counter.size())+1+index)) {
				cout << counter[0] << endl;
				index--;
			}
			if (index < 0) {
				complete = true;
			} else {
				counter[index]++;
				for (int j=index+1;j < int(counter.size()); j++) {
					counter[j] = counter[j-1]+1;
				}
			}
		}
	}
	this->RelaxIntegerVariables = false;
	SetParameter("Essential Gene List",essentialList.data());
	Output.close();
	return SUCCESS;
}

int MFAProblem::FindTightBounds(Data* InData,OptimizationParameter*& InParameters,string Note) {
	FindTightBounds(InData,InParameters,GetParameter("Save and print TightBound solutions").compare("1") == 0,true);
	Note.append("Found tight bounds with no objective attached");
	PrintProblemReport(0,InParameters,Note);
	return SUCCESS;
}

int MFAProblem::AlternativeSolutionExploration(OptimizationParameter* InParameters,string Filename,OptSolutionData* InitialSolution,bool clearmarks) {
	//At this point we branch out if the user asked for alternative analysis instead of standard recursive MILP
	int TotalSolutions = 1;
	bool OriginalSense = FMax();
	LinEquation* OriginalObj = ObjFunct;
	//Identifying all variables used in the intial solution
	vector<MFAVariable*> InitialSolutionVariables;
	vector<int> InitialVariableIndecies;
	//Identifying alternatives to each variable in the original solution
	if (InitialSolution == NULL) {
		InitialSolution = RunSolver(true,true,true);
	}
	LinEquation* SolutionVarSumObj = InitializeLinEquation();
	bool binaryvar = false;
	vector<bool> objmarks;
	vector<double> upperbnds;
	vector<double> lowerbnds;
	for (int i=0; i < int(ObjFunct->Variables.size()); i++) {
		objmarks.push_back(false);
		upperbnds.push_back(ObjFunct->Variables[i]->UpperBound);
		lowerbnds.push_back(ObjFunct->Variables[i]->LowerBound);
		double threshold = MFA_ZERO_TOLERANCE;
		if (ObjFunct->Variables[i]->Binary) {
			threshold = 0.5;
			binaryvar = true;
		}
		if (InitialSolution->SolutionData[ObjFunct->Variables[i]->Index] > threshold) {
			InitialSolutionVariables.push_back(ObjFunct->Variables[i]);
			InitialVariableIndecies.push_back(i);
			if (clearmarks) {
				ObjFunct->Variables[i]->Mark = false;
			}
			objmarks[i] = true;
		}
	}
	vector<vector<vector<string> > > solalts;
	vector<string> essentials;
	for (int i=0; i < int(InitialSolutionVariables.size()); i++) {
		if (!InitialSolutionVariables[i]->Mark) {
			cout << "Variable " << i << " of " << int(InitialSolutionVariables.size()) << " with name " << InitialSolutionVariables[i]->Name << endl;
			ObjFunct = CloneLinEquation(OriginalObj);
			vector<string> group;
			vector<vector<string> > alternatives;
			vector<int> groupindecies;
			groupindecies.push_back(InitialVariableIndecies[i]);
			group.push_back(InitialSolutionVariables[i]->Name);
			//Setting bounds on target variable to zero
			InitialSolutionVariables[i]->UpperBound = 0;
			LoadVariable(InitialSolutionVariables[i]->Index);
			//Setting the lower bound on all other variables to non-zero
			for (int j=0; j < int(InitialSolutionVariables.size()); j++) {
				if (j != i) {
					if (InitialSolutionVariables[j]->Binary) {
						InitialSolutionVariables[j]->LowerBound = 1;
						LoadVariable(InitialSolutionVariables[j]->Index);
					} else {
						//Don't want to actualy set lower bound - instead, let's make the variable very cheap
						ObjFunct->Coefficient[InitialVariableIndecies[j]] = 0.0001*ObjFunct->Coefficient[InitialVariableIndecies[j]];
					}
				}
			}
			LoadObjective();
			OptSolutionData* NewSolution = RunSolver(true,true,true);
			if (NewSolution->Status != SUCCESS) {
				essentials.push_back(InitialSolutionVariables[i]->Name);
				cout << "Variable " << i << " essential!" << endl;
			} else {
				for (int j=0; j < int(InitialSolutionVariables.size()); j++) {
					if (j != i) {
						double threshold = MFA_ZERO_TOLERANCE;
						if (InitialSolutionVariables[j]->Binary) {
							threshold = 0.5;
						}
						if (NewSolution->SolutionData[InitialSolutionVariables[j]->Index] < threshold && upperbnds[InitialVariableIndecies[j]] > 0) {
							group.push_back(InitialSolutionVariables[j]->Name);
							groupindecies.push_back(InitialVariableIndecies[j]);
							InitialSolutionVariables[j]->UpperBound = 0;
							LoadVariable(InitialSolutionVariables[j]->Index);
							ObjFunct->Coefficient[InitialVariableIndecies[j]] = ObjFunct->Coefficient[InitialVariableIndecies[j]]*10000;
							InitialSolutionVariables[j]->Mark = true;
						}
					}
				}
				cout << "Group size is " << group.size() << endl;
				LoadObjective();
				alternatives.push_back(group);
				for (int k=0; k < InParameters->RecursiveMILPSolutionLimit; k++) {
					vector<string> othersol;
					for (int j=0; j < int(ObjFunct->Variables.size()); j++) {
						double threshold = MFA_ZERO_TOLERANCE;
						if (ObjFunct->Variables[j]->Binary) {
							threshold = 0.5;
							binaryvar = true;
						}
						if (objmarks[j] == false && NewSolution->SolutionData[ObjFunct->Variables[j]->Index] > threshold) {
							othersol.push_back(ObjFunct->Variables[j]->Name);
							ObjFunct->Variables[j]->UpperBound = 0;
							LoadVariable(ObjFunct->Variables[j]->Index);
						}
					}
					if (othersol.size() == 0) {
						k = 1000;
						cout << "Group is completely expendable!" << endl;
						for (int j=0; j < int(groupindecies.size()); j++) {
							cout << "Permanent KO of variable " << group[j] << endl;
							upperbnds[groupindecies[j]] = 0;//Permanently KOing this group variables
						}
					} else {
						cout << "Alternative " << k << " size is " << othersol.size() << endl;
						alternatives.push_back(othersol);
						NewSolution = RunSolver(true,true,true);
						if (NewSolution->Status != SUCCESS) {
							k = 1000;
						}
					}
				}
				if (alternatives.size() > 1) {
					solalts.push_back(alternatives);
				}
			}
			for (int j=0; j < int(ObjFunct->Variables.size()); j++) {
				ObjFunct->Variables[j]->UpperBound = upperbnds[j];
				ObjFunct->Variables[j]->LowerBound = lowerbnds[j];
				LoadVariable(ObjFunct->Variables[j]->Index);
			}
		}
	}
	Filename = FOutputFilepath()+Filename;
	ofstream output(Filename.data());
	output << "Essentials:";
	for (int i=0; i < int(essentials.size()); i++) {
		if (i > 0) {
			output << "\t";
		}
		output << essentials[i];
	}
	output << endl;
	for (int i=0; i < int(solalts.size()); i++) {
		output << i << ":";
		for (int j=0; j < int(solalts[i].size()); j++) {
			if (j > 0) {
				output << "\t";
			}
			for (int k=0; k < int(solalts[i][j].size()); k++) {
				if (k > 0) {
					output << ";";
				}
				output << solalts[i][j][k];
			}
		}
		output << endl;
	}
	output.close();
}

int MFAProblem::RecursiveMILP(Data* InData, OptimizationParameter*& InParameters, vector<int> VariableTypes,bool PrintSolutions) {	
	int Status = SUCCESS;

	//At least one binary variable type must be specified in order to do recursive MILP
	if (VariableTypes.size() == 0) {
		FErrorFile() << "No variable types provided to recursive MILP. Cannot continue." << endl;
		FlushErrorFile();
		return FAIL;	
	}

	//If we are minimizing the addition of foreign reactions, then only foreign reaction use variables should be manipulated
	bool ForeignOnly = false;
	if (InParameters->LoadForeignDB && InParameters->MinimizeForeignReactions) {
		ForeignOnly = true;
	}

	//If there are no variables, the MFA problem object has not been loaded yet from the database
	if (FNumVariables() == 0) {
		if (BuildMFAProblem(InData,InParameters) != SUCCESS) {
			FErrorFile() << "Failed to build optimization problem required to determine tight bounds." << endl;
			FlushErrorFile();
			return FAIL;	
		}
	}

	//Starting the clock so that the specified timelimit for the recursive MILP will be respected
	int ClockIndex = StartClock(-1);
	SetTimeout(ClockIndex, atof(GetParameter("Recursive MILP timeout").data()));
	
	//Creating the problem note
	string ProblemNote("Recursive MILP ");

	//Saving the problem state
	int ProblemStateIndex = SaveState();

	//Dealing with the current solutions for this problem based on input parameters
	if (PrintSolutions) {
		Solutions.clear();
	}
	
	//I create the optimization solution object
	OptSolutionData* NewSolution = NULL;
	
	//I create the sum objective object as well, incase objective switching is required
	LinEquation* SumObjective = NULL;

	//I save the original objective for use in reoptimizations if desired
	LinEquation* OriginalObjective = ObjFunct;
	
	if (VariableTypes.size() >= 1 && VariableTypes[0] == OBJECTIVE_TERMS) {
		//The current objective is the correct objective for the recursive MILP and should not be altered
		SumObjective = ObjFunct;
		//The problem also should not be reloaded
	} else {
		//I NULL out the original objective to keep it from being deleted
		ObjFunct = NULL;
		
		//I change the objective to minimizing the sum of the integer variables requested by the user
		AddSumObjective(VariableTypes[0], false, false, 1, ForeignOnly);
		ProblemNote.append(ConvertVariableType(VariableTypes[0]));
		for (int i=1; i < int(VariableTypes.size()); i++) {
			ProblemNote.append(",");
			ProblemNote.append(ConvertVariableType(VariableTypes[i]));
			AddSumObjective(VariableTypes[i], false, true, 1, ForeignOnly);
		}
		ProblemNote.append(" optimization:");
		SetMin();
		SumObjective = GetObjective();
	
		//Now I load the solver or refresh the solver depending on whether or not I already optimized the initial objective
		if (LoadSolver() != SUCCESS) {
			LoadState(ProblemStateIndex,true,true,true,false,true);
			ClearState(ProblemStateIndex);
			ClearClock(ClockIndex);
			return FAIL;
		}
	}

	//I perform the initial minimization to determine the absolute minimum number of reactions
	//This number is used to enforce the solution size interval selected by the user
	NewSolution = RunSolver(true,false,true);
	if (NewSolution->Status != SUCCESS) {
		FErrorFile() << "Initial optimization failed. Cannot continue." << endl;
		FlushErrorFile();
		LoadState(ProblemStateIndex,true,true,true,false,true);
		ClearState(ProblemStateIndex);
		ClearClock(ClockIndex);
		return FAIL;
	}
	NewSolution->Notes.assign("Recursive milp solution");
	int MinReactions = int(NewSolution->Objective);

	//Now if the user allowed for suboptimal initial objective values, I would like to determine the optimal objective value for this new solution
	//To do this, I must set bounds on the variables to enforce the current solution, reset the objective to the original objective, and optimize the original objective
	if (InParameters->OptimalObjectiveFraction < 0 && InParameters->ReoptimizeSubOptimalObjective) {
		ObjFunct = OriginalObjective;
		LoadObjective();
		EnforceIntergerSolution(NewSolution,VariableTypes,ForeignOnly,true);
		OptSolutionData* TempSolution = RunSolver(true,false,true);
		TempSolution->Notes.assign("Optimal objective for previous MILP solution");
		ObjFunct = SumObjective;
		LoadObjective();
		RelaxSolutionEnforcement(VariableTypes,ForeignOnly,true);
	}

	//At this point we branch out if the user asked for alternative analysis instead of standard recursive MILP
	int TotalSolutions = 1;
	if (InParameters->AlternativeSolutionAlgorithm) {
		bool OriginalSense = FMax();
		//Identifying all variables used in the intial solution
		vector<MFAVariable*> InitialSolutionVariables;
		//Identifying alternatives to each variable in the original solution
		LinEquation* SolutionVarSumObj = InitializeLinEquation();
		for (int i=0; i < int(ObjFunct->Variables.size()); i++) {
			if (NewSolution->SolutionData[ObjFunct->Variables[i]->Index] > 0.5) {
				for (int j=0; j < int(VariableTypes.size()); j++) {
					if (ObjFunct->Variables[i]->Type == VariableTypes[j]) {
						InitialSolutionVariables.push_back(ObjFunct->Variables[i]);
						ObjFunct->Variables[i]->Mark = false;
						break;
					}
				}
			}
		}
		
		for (int i=0; i < int(InitialSolutionVariables.size()); i++) {
			if (!InitialSolutionVariables[i]->Mark) {
				//Saving the initial reactions as the first alternative
				if (i > 0) {
					ProblemNote.append("|");
				}
				ProblemNote.append(InitialSolutionVariables[i]->Name);
				//Setting bounds on target variable to zero
				InitialSolutionVariables[i]->UpperBound = 0;
				LoadVariable(InitialSolutionVariables[i]->Index);
				//Setting the lower bound on all other variables to one
				for (int j=0; j < int(InitialSolutionVariables.size()); j++) {
					if (j != i) {
						InitialSolutionVariables[j]->LowerBound = 1;
						LoadVariable(InitialSolutionVariables[j]->Index);
					}
				}
				//Now running fundamental recursive MILP to find all alternatives to the target reaction(s)
				vector<OptSolutionData*> RecMILPSolutions = RecursiveMILP(InParameters,ProblemNote,ForeignOnly,VariableTypes,MinReactions,ClockIndex,OriginalObjective);
				//Cycling through the output solutions and noting alternative reactions
				vector<string> Alternatives(RecMILPSolutions.size(),"");
				map<MFAVariable*,vector<bool> > LinkedVariables;
				for (int j=0; j < int(RecMILPSolutions.size()); j++) {
					for (int k=0; k < int(SumObjective->Variables.size()); k++) {
						if (RecMILPSolutions[j]->SolutionData[SumObjective->Variables[k]->Index] > 0.5) {
							if (SumObjective->Variables[k]->LowerBound != 1) {
								for (int m=0; m < int(VariableTypes.size()); m++) {
									if (SumObjective->Variables[k]->Type == VariableTypes[m]) {
										if (Alternatives[j].length() > 0) {
											Alternatives[j].append(",");
										}
										Alternatives[j].append(SumObjective->Variables[k]->Name);
										break;
									}
								}
							} else if (SumObjective->Variables[k]->AssociatedReaction != NULL && (SumObjective->Variables[k]->Type == FORWARD_USE || SumObjective->Variables[k]->Type == REACTION_USE)) {
								//Looking for variables that are turned on but had zero corresponding flux
								MFAVariable* FluxVar = SumObjective->Variables[k]->AssociatedReaction->GetMFAVar(FLUX);
								if (FluxVar == NULL) {
									FluxVar = SumObjective->Variables[k]->AssociatedReaction->GetMFAVar(FORWARD_FLUX);
								}
								if (FluxVar != NULL && RecMILPSolutions[j]->SolutionData[FluxVar->Index] < MFA_ZERO_TOLERANCE) {
									if (LinkedVariables[SumObjective->Variables[k]].size() == 0) {
										LinkedVariables[SumObjective->Variables[k]].resize(RecMILPSolutions.size(),false);
									}
									LinkedVariables[SumObjective->Variables[k]][j] = true;
								}
							} else if (SumObjective->Variables[k]->AssociatedReaction != NULL && SumObjective->Variables[k]->Type == REVERSE_USE) {
								//Looking for variables that are turned on but had zero corresponding flux
								MFAVariable* FluxVar = SumObjective->Variables[k]->AssociatedReaction->GetMFAVar(REVERSE_FLUX);
								if (FluxVar != NULL && RecMILPSolutions[j]->SolutionData[FluxVar->Index] < MFA_ZERO_TOLERANCE) {
									if (LinkedVariables[SumObjective->Variables[k]].size() == 0) {
										LinkedVariables[SumObjective->Variables[k]].resize(RecMILPSolutions.size(),false);
									}
									LinkedVariables[SumObjective->Variables[k]][j] = true;
								}
							}
						}
					}
				}
				for (map<MFAVariable*,vector<bool> >::iterator MapIT = LinkedVariables.begin(); MapIT != LinkedVariables.end(); MapIT++) {
					vector<bool> Temp = MapIT->second;
					bool Universal = true;
					for (int j=0; j < int(Temp.size()); j++) {
						if (!Temp[j]) {
							Universal = false;
							for (int k=0; k < int(Temp.size()); k++) {
								if (Temp[k]) {
									Alternatives[k].append(",("+MapIT->first->Name+")");
								}
							}
							break;
						}
					}
					if (Universal) {
						MapIT->first->Mark = true;
						ProblemNote.append(","+MapIT->first->Name);
					}
				}
				for (int j=0; j < int(Alternatives.size()); j++) {
					ProblemNote.append(":"+Alternatives[j]);
				}
				//Restoring original bounds on all variables
				for (int j=0; j < int(InitialSolutionVariables.size()); j++) {
					InitialSolutionVariables[j]->UpperBound = 1;
					InitialSolutionVariables[j]->LowerBound = 0;
					LoadVariable(InitialSolutionVariables[j]->Index);
				}
			}
		}

		SetParameter("Current gap filling solutions",ProblemNote.data());	
		PrintProblemReport(NewSolution->Objective,InParameters,ProblemNote);
		//		PrintProblemReport(MinReactions,InParameters,ProblemNote);
		ClearSolutions();

		delete SolutionVarSumObj;	
	} else {
		vector<OptSolutionData*> RecMILPSolutions;
		RecMILPSolutions.push_back(NewSolution);
		if (InParameters->RecursiveMILPSolutionLimit > 1) {
			//I add the solution constraint so that the current solution will not be generated again
			if (LoadConstToSolver(AddUseSolutionConst(NewSolution,VariableTypes,InParameters)->Index) != SUCCESS) {
				FErrorFile() << "Could not add use solution constraint to solver." << endl;
				FlushErrorFile();
				LoadState(ProblemStateIndex,true,true,true,false,true);
				ClearState(ProblemStateIndex);
				ClearClock(ClockIndex);
				return FAIL;
			}
			if (InParameters->GapGeneration) {
				ResetSolver();
				LoadSolver(false);
			}

			int CurrentSolution = MinReactions;
			if (InParameters->RecursiveMILPSolutionLimit == -1) {
				InParameters->RecursiveMILPSolutionLimit = 100000;
			}
			vector<OptSolutionData*> Temp = RecursiveMILP(InParameters,ProblemNote,ForeignOnly,VariableTypes,MinReactions,ClockIndex,OriginalObjective);
			for (int i=0; i < int(Temp.size()); i++) {
				RecMILPSolutions.push_back(Temp[i]);
			}
			TotalSolutions = TotalSolutions+int(RecMILPSolutions.size());
		}
		//Printing the problem data if requested
		if (PrintSolutions) {
			//Adding the specific solutions found to the problem note
			for (int i=0; i < int(RecMILPSolutions.size()); i++) {
				if (RecMILPSolutions[i]->Notes.compare("Recursive milp solution") == 0) {
					string CurrentSolutionString(dtoa(RecMILPSolutions[i]->Objective));
					CurrentSolutionString.append(":");
					bool First = true;
					//Scanning through the objective variables
					for (int j=0; j < int(ObjFunct->Variables.size()); j++) {
						if (InParameters->GapGeneration) {
							if (RecMILPSolutions[i]->SolutionData[ObjFunct->Variables[j]->Index] < 0.5 && ObjFunct->Variables[j]->Type != COMPLEX_USE) {
								if (!First) {
									CurrentSolutionString.append(",");
								}
								CurrentSolutionString.append(ObjFunct->Variables[j]->Name);
								First = false;
							}
						} else if (RecMILPSolutions[i]->SolutionData[ObjFunct->Variables[j]->Index] > 0.5 && ObjFunct->Variables[j]->Type != COMPLEX_USE) {
							if (!InParameters->GapFilling || ObjFunct->Variables[j]->Type < SMALL_DELTAG_ERROR_USE) { 
								if (!First) {
									CurrentSolutionString.append(",");
								}
								CurrentSolutionString.append(ObjFunct->Variables[j]->Name);
								First = false;
							}
						}
					}
					CurrentSolutionString.append("|");
					SetParameter("Current gap filling solutions",(GetParameter("Current gap filling solutions")+CurrentSolutionString).data());
					ProblemNote.append(CurrentSolutionString);
				} else {
					ProblemNote.append(dtoa(RecMILPSolutions[i]->Objective));
					ProblemNote.append("|");
				}
			}
			ProblemNote.append(")");
			PrintProblemReport(NewSolution->Objective, InParameters, ProblemNote);
			//			PrintProblemReport(MinReactions,InParameters,ProblemNote);
			ClearSolutions();
		}
	}
	//Restoring problem state
	LoadState(ProblemStateIndex,true,true,true,false,true);
	ClearState(ProblemStateIndex);
	ResetSolver();
	LoadSolver();

	//Clearing the clock for this recursive MILP
	ClearClock(ClockIndex);
	return TotalSolutions;
}

vector<OptSolutionData*> MFAProblem::RecursiveMILP(OptimizationParameter* InParameters, string ProblemNote,bool ForeignOnly,vector<int> VariableTypes,double MinSolution,int ClockIndex,LinEquation* OriginalObjective) {
	int Status = SUCCESS;
	vector<OptSolutionData*> SolutionSet;
	int TotalSolutions = 1;
	LinEquation* SumObjective = ObjFunct;
	OptSolutionData* NewSolution = NULL;
	
	while (1) {
		NewSolution = RunSolver(true,false,true);
		//Checking if the solution is not viable
		if (NewSolution == NULL || NewSolution->Status != SUCCESS) {
			if (NewSolution != NULL) {
				delete NewSolution;
			}
			cout << "Breaking because there are no more solutions!!" << endl;
			break;
		}
		SolutionSet.push_back(NewSolution);
		//I add the solution constraint so that the current solution will not be generated again
		if (LoadConstToSolver(AddUseSolutionConst(NewSolution,VariableTypes,InParameters)->Index) != SUCCESS) {
			FErrorFile() << "Could not add use solution constraint to solver." << endl;
			FlushErrorFile();
			break;
		}
		//Now if the user allowed for suboptimal initial objective values, I would like to determine the optimal objective value for this new solution
		if (InParameters->OptimalObjectiveFraction < 0 && !InParameters->SimultaneouslyMinReactionsMaxObjective) {
			ObjFunct = OriginalObjective;
			LoadObjective();
			EnforceIntergerSolution(NewSolution,VariableTypes,ForeignOnly,true);
			OptSolutionData* TempSolution = RunSolver(true,false,true);
			TempSolution->Notes.assign("Optimal objective for previous MILP solution");
			ObjFunct = SumObjective;
			LoadObjective();
			RelaxSolutionEnforcement(VariableTypes,ForeignOnly,true);
		}
		NewSolution->Notes.assign("Recursive milp solution");
		TotalSolutions++;
		double CurrentSolution = int(NewSolution->Objective);
		//Checking for time out
		if (TimedOut(ClockIndex)) {
			FErrorFile() << "Recursive MILP timed out: " << ProblemNote << endl;
			FlushErrorFile();
			cout << "Breaking due to time out!!" << endl;
			break;
		}
		//Checking for the solution limit
		if (InParameters->RecursiveMILPSolutionLimit != -1 && TotalSolutions > InParameters->RecursiveMILPSolutionLimit) {
			FErrorFile() << "Recursive MILP solution limit hit: " << ProblemNote << endl;
			FlushErrorFile();
			cout << "Breaking due to solution limit!!" << endl;
			break;
		}
		//Checking for solution interval limit
		if (CurrentSolution - MinSolution >= InParameters->SolutionSizeInterval+MFA_ZERO_TOLERANCE) {
			SolutionSet.pop_back();
			ClearSolutions(FNumSolutions()-1);
			FErrorFile() << "Recursive MILP solution interval hit: " << ProblemNote << endl;
			FlushErrorFile();
			cout << "Breaking due to solution interval!!" << endl;
			break;
		}
		if (InParameters->GapGeneration) {
			ResetSolver();
			LoadSolver(false);
		}
	};
	
	return SolutionSet;
}

int MFAProblem::CheckIndividualMetaboliteProduction(Data* InData, OptimizationParameter* InParameters, vector<Species*> Metabolites, vector<int> Compartments, bool FindTightBounds, bool MinimizeForeignReactions, bool MakeAllDrainsSimultaneously, string Note, bool SubProblem) {
	bool OriginalPrint = InParameters->PrintSolutions;
	bool OriginalClear = InParameters->ClearSolutions;
	InParameters->PrintSolutions = false;
	InParameters->ClearSolutions = false;
	
	bool First = true;
	int Status = SUCCESS;
	//If the problem is not already built, I build the problem
	if (FNumVariables() == 0) {
		if (BuildMFAProblem(InData,InParameters) != SUCCESS) {
			FErrorFile() << "Failed to build optimization problem." << endl;
			FlushErrorFile();
			Note.append("Failed to build optimization problem");
			PrintProblemReport(FLAG,InParameters,Note);
			return FAIL;	
		}
	}
	//I clear the previous objective and create the new objective
	ClearObjective(false);
	ObjFunct = InitializeLinEquation();
	ObjFunct->Coefficient.push_back(1);
	ObjFunct->Variables.push_back(NULL);

	//First I create all of the drain variables
	vector<double> OriginalLowerBounds;
	vector<double> OriginalUpperBounds;
	vector<MFAVariable*> DrainVariables;
	for (int i=0; i < int(Metabolites.size()); i++) {
		Species* CurrentSpecies = Metabolites[i];
		MFAVariable* DrainVariable;
		if (Compartments[i] >= 0) {
			DrainVariable = CurrentSpecies->GetMFAVar(DRAIN_FLUX,Compartments[i]);
		} else {
			DrainVariable = CurrentSpecies->GetMFAVar(DRAIN_FLUX,-(Compartments[i]+1));
		}
		int VarConst = -1;
		bool Continue = true;
		if (DrainVariable == NULL) {
			if (Compartments[i] >= 0) {
				DrainVariable = CurrentSpecies->CreateMFAVariable(DRAIN_FLUX,Compartments[i],-100,0);
			} else {
				DrainVariable = CurrentSpecies->CreateMFAVariable(DRAIN_FLUX,-(Compartments[i]+1),0,100);
			}
			AddVariable(DrainVariable);
			if (ProblemLoaded) {
				LoadVariable(DrainVariable->Index);
			}
			DrainVariable->AssociatedSpecies = CurrentSpecies;
			for (int j=0; j < FNumConstraints(); j++) {
				if (GetConstraint(j)->ConstraintMeaning.substr(2,GetConstraint(j)->ConstraintMeaning.length()-2).compare("mass_balance") == 0 && GetConstraint(j)->AssociatedSpecies == CurrentSpecies) {
					VarConst = j;
					GetConstraint(j)->Variables.push_back(DrainVariable);
					GetConstraint(j)->Coefficient.push_back(1);
					if (ProblemLoaded) {
						LoadConstToSolver(GetConstraint(j)->Index);
					}
					break;
				}
			}
			if (VarConst != -1) {
				DrainVariables.push_back(DrainVariable);
				OriginalLowerBounds.push_back(0);
				OriginalUpperBounds.push_back(0);
				if (!MakeAllDrainsSimultaneously) {
					DrainVariable->LowerBound = 0;
					DrainVariable->UpperBound = 0;
				}
				
			}
		} else {
			OriginalLowerBounds.push_back(DrainVariable->LowerBound);
			OriginalUpperBounds.push_back(DrainVariable->UpperBound);
			if (Compartments[i] >= 0) {
				DrainVariable->UpperBound = 0;
				DrainVariable->LowerBound = -100;
			} else {
				DrainVariable->UpperBound = 100;
				DrainVariable->LowerBound = 0;
			}
			if (!MakeAllDrainsSimultaneously) {
				DrainVariable->LowerBound = 0;
				DrainVariable->UpperBound = 0;
			}
			DrainVariables.push_back(DrainVariable);
			if (ProblemLoaded) {
				LoadVariable(DrainVariable->Index);
			}
			DrainVariable->AssociatedSpecies = CurrentSpecies;
		}
	}

	
	//I set the objective sense to minimize because we want to minimize drain flux to maximize production
	SetMin();

	//Now I optimize each individual drain variable
	SetParameter("No growth metabolites","");
	ofstream Output;
	OpenOutput(Output,FOutputFilepath()+"MFAOutput/MetaboliteProduction.txt");
	Output << "Metabolite;Maximum production" << endl;
	for (int i=0; i < int(DrainVariables.size()); i++) {	
		if (Compartments[i] >= 0) {
			DrainVariables[i]->UpperBound = 0;
			DrainVariables[i]->LowerBound = -100;
		
			ObjFunct->Variables[0] = DrainVariables[i];
			//Now I load the solver if it is not already loaded
			if (!ProblemLoaded) {
				if (LoadSolver() != SUCCESS) {
					Note.append("Failed to load optimization problem");
					PrintProblemReport(FLAG,InParameters,Note);
					return FAIL;	
				}
			} else {
				LoadVariable(DrainVariables[i]->Index);
				LoadObjective();
			}
			double CurrentObjective = 0;
			//string NewNote(Note);
			//NewNote.append("Optimize metabolite production: ");
			//NewNote.append(DrainVariables[i]->AssociatedSpecies->GetData("DATABASE",STRING));
			//Status = OptimizeSingleObjective(InData,InParameters,FindTightBounds,MinimizeForeignReactions,CurrentObjective,NewNote,SubProblem);
			OptSolutionData* NewSolution = RunSolver(false,true,false);
			if (NewSolution != NULL && NewSolution->Status == SUCCESS) {
				if (fabs(NewSolution->Objective) < 1e-7) {
					SetParameter("No growth metabolites",(GetParameter("No growth metabolites")+DrainVariables[i]->AssociatedSpecies->GetData("DATABASE",STRING)+";").data());
				}
				Output << DrainVariables[i]->AssociatedSpecies->GetData("DATABASE",STRING) << ";" << NewSolution->Objective << endl;
				if (!MakeAllDrainsSimultaneously) {
					DrainVariables[i]->LowerBound = OriginalLowerBounds[i];
					DrainVariables[i]->UpperBound = OriginalUpperBounds[i];
					LoadVariable(DrainVariables[i]->Index);
				}
			}
		}
	}
	Output.close();

	if (MakeAllDrainsSimultaneously) {
		for (int i=0; i < int(DrainVariables.size()); i++) {
			DrainVariables[i]->LowerBound = OriginalLowerBounds[i];
			DrainVariables[i]->UpperBound = OriginalUpperBounds[i];
			LoadVariable(DrainVariables[i]->Index);
		}
	}

	if (OriginalPrint && !SubProblem) {
		PrintSolutions(-1,-1);
	}
	if (OriginalClear && !SubProblem) {
		ClearSolutions();
	}
	InParameters->PrintSolutions = OriginalPrint;
	InParameters->ClearSolutions = OriginalClear;

	return Status;
}

int MFAProblem::CheckIndividualMetaboliteProduction(Data* InData, OptimizationParameter* InParameters, string InMetaboliteList, bool FindTightBounds, bool MinimizeForeignReactions, string Note, bool SubProblem) {
	bool MakeAllDrainsSimultaneously = false;
	
	vector<string>* Strings = StringToStrings(InMetaboliteList,";");
	vector<Species*> Targets;
	vector<int> Compartments;
	if (Strings->size() > 0) {
		Reaction* NewReaction = NULL;
		double OriginalMin;
		double OriginalMax;
		if ((*Strings)[0].compare("OBJECTIVE") == 0 && this->GetObjective() != NULL) {
			LinEquation* originalObjective = this->GetObjective();
			for (int i=0; i < int(originalObjective->Variables.size()); i++) {
				if (originalObjective->Coefficient[i] != 0) {
					if (originalObjective->Variables[i]->AssociatedSpecies != NULL) {
						Compartments.push_back(originalObjective->Variables[i]->Compartment);
						Targets.push_back(originalObjective->Variables[i]->AssociatedSpecies);
					} else if (originalObjective->Variables[i]->AssociatedReaction != NULL) {
						for (int j=0; j < originalObjective->Variables[i]->AssociatedReaction->FNumReactants(REACTANT); j++) {
							Targets.push_back(originalObjective->Variables[i]->AssociatedReaction->GetReactant(j));
							Compartments.push_back(originalObjective->Variables[i]->AssociatedReaction->GetReactantCompartment(j));
						}
						for (int j=originalObjective->Variables[i]->AssociatedReaction->FNumReactants(REACTANT); j < originalObjective->Variables[i]->AssociatedReaction->FNumReactants(); j++) {
							Targets.push_back(originalObjective->Variables[i]->AssociatedReaction->GetReactant(j));
							Compartments.push_back(-originalObjective->Variables[i]->AssociatedReaction->GetReactantCompartment(j)-1);
						}
						originalObjective->Variables[i]->AssociatedReaction->ResetFluxBounds(0,100,this);
					}
				}
			}
		} else if ((*Strings)[0].compare("REACTANTS") == 0) {
			MakeAllDrainsSimultaneously = true;
			NewReaction = InData->FindReaction("NAME;DATABASE;ENTRY",(*Strings)[1].data());
			if (NewReaction != NULL) {
				OriginalMin = NewReaction->FluxLowerBound();
				OriginalMax = NewReaction->FluxUpperBound();
				NewReaction->ResetFluxBounds(0,100,this);
				ResetSolver();

				for (int i=0; i < NewReaction->FNumReactants(REACTANT); i++) {
					Targets.push_back(NewReaction->GetReactant(i));
					Compartments.push_back(NewReaction->GetReactantCompartment(i));
				}
				for (int i=NewReaction->FNumReactants(REACTANT); i < NewReaction->FNumReactants(); i++) {
					Targets.push_back(NewReaction->GetReactant(i));
					Compartments.push_back(-NewReaction->GetReactantCompartment(i)-1);
				}
			}
		} else if ((*Strings)[0].compare("ALL") == 0) {
			MakeAllDrainsSimultaneously = false;
			for (int i=0; i < InData->FNumSpecies(); i++) {
				Targets.push_back(InData->GetSpecies(i));
				Compartments.push_back(GetDefaultCompartment()->Index);
			}
		} else {
			MakeAllDrainsSimultaneously = false;
			for (int i=0; i < int(Strings->size()); i++) {
				Species* NewSpecies = InData->FindSpecies("NAME;DATABASE;ENTRY",(*Strings)[i].data());
				if (NewSpecies != NULL) {
					Targets.push_back(NewSpecies);
					Compartments.push_back(GetDefaultCompartment()->Index);
				}
			}
		}
		int Status = CheckIndividualMetaboliteProduction(InData,InParameters,Targets,Compartments,FindTightBounds,MinimizeForeignReactions,MakeAllDrainsSimultaneously,Note, SubProblem);
		ResetSolver();
		if (NewReaction != NULL) {
			NewReaction->ResetFluxBounds(OriginalMin,OriginalMax,this);
		}
		return Status;
	}
	delete Strings;
	return FAIL;
}

int MFAProblem::RunDeletionExperiments(Data* InData,OptimizationParameter* InParameters,bool GapfillPhenosim) {
	SavedBounds* originalBounds = this->saveBounds();
	//Running expriments
	vector<string> outputVector;
	OptSolutionData* NewSolution = NULL;
	outputVector.push_back("Label\tGenes\tKO reactions\tMedia\tWT growth\tGrowth\tNo growth metabolites\tNew inactive reactions\tNew essential genes\tFluxes");
	InParameters->MaxDrainFlux = 0;
	this->clearOldMedia(InParameters);
	SavedBounds* currentBounds = this->saveBounds();
	bool originalSense = this->FMax();
	LinEquation* originalObjective = this->GetObjective();
	LinEquation* gapfillobj = this->GetObjective();
	if (GapfillPhenosim) {
		originalObjective = this->ObjectiveConstraint;
		for (int i=0; i < int(gapfillobj->Variables.size()); i++) {
			if (gapfillobj->Coefficient[i] > 0) {
				if (gapfillobj->Variables[i]->Type == FLUX || gapfillobj->Variables[i]->Type == FORWARD_FLUX || gapfillobj->Variables[i]->Type == REVERSE_FLUX) {
					gapfillobj->Variables[i]->Exclude = true;
				}
			}
		}
	}
	map<string,bool> inactiveReactions;
	map<string,bool> compoundsToAssess;
	this->RelaxIntegerVariables = true;
	if (this->Solver != CPLEX) {
		this->Solver = GLPK;
	}
	this->LoadSolver();
	SetParameter("tight bounds search variables","FLUX;FORWARD_FLUX;REVERSE_FLUX;DRAIN_FLUX;FORWARD_DRAIN_FLUX;REVERSE_DRAIN_FLUX");
	bool essentialrxnko = false;
	string essentialmedia;
	if (InParameters->labels.size() == 1 && InParameters->labels[0].compare("essentialreactions") == 0) {
		essentialrxnko = true;
		essentialmedia = InParameters->mediaConditions[0];
		InParameters->labels.clear();
		InParameters->mediaConditions.clear();
		InParameters->KOSets.clear();
	}
	if (GetParameter("find tight bounds").compare("1") == 0) {
		this->FindTightBounds(InData,InParameters,false,true);
		for (int i=0; i < FNumVariables(); i++) {
			if ((GetVariable(i)->AssociatedReaction != NULL && GetVariable(i)->Type == FLUX) || (GetVariable(i)->AssociatedSpecies != NULL && GetVariable(i)->Type == DRAIN_FLUX && GetVariable(i)->Compartment == GetCompartment("c")->Index)) {
				if (GetVariable(i)->AssociatedSpecies != NULL) {
					compoundsToAssess[GetVariable(i)->AssociatedSpecies->GetData("DATABASE",STRING)] = true;
				}
				if (fabs(GetVariable(i)->Max) < 1e-7 && fabs(GetVariable(i)->Min) < 1e-7) {
					if (GetVariable(i)->AssociatedReaction != NULL) {
						inactiveReactions[GetVariable(i)->AssociatedReaction->GetData("DATABASE",STRING)] = true;
					} else {
						inactiveReactions[GetVariable(i)->AssociatedSpecies->GetData("DATABASE",STRING)] = true;
					}
				}
			}
		}
		this->ObjFunct = originalObjective;
	}
	LinEquation* objectiveConstraint = this->MakeObjectiveConstraint(-10000,GREATER);
	if (essentialrxnko) {
		objectiveConstraint->RightHandSide = 0.1;
		this->loadMedia(InParameters->mediaConditions[0],InData,true);
		this->LoadSolver();
		this->FindTightBounds(InData,InParameters,false,true);
		for (int i=0; i < FNumVariables(); i++) {
			if (GetVariable(i)->AssociatedReaction != NULL && GetVariable(i)->Type == FLUX) {
				if (essentialrxnko && (GetVariable(i)->Max < -1e-7 || GetVariable(i)->Min > 1e-7)) {
					if (originalObjective->Variables[0]->AssociatedReaction != GetVariable(i)->AssociatedReaction) {
						InParameters->labels.push_back(GetVariable(i)->AssociatedReaction->GetData("DATABASE",STRING));
						InParameters->mediaConditions.push_back(essentialmedia);
						vector<string> reactionlist;
						cout << "Essential rxn:" << GetVariable(i)->AssociatedReaction->GetData("DATABASE",STRING) << endl;
						reactionlist.push_back(GetVariable(i)->AssociatedReaction->GetData("DATABASE",STRING));
						InParameters->KOSets.push_back(reactionlist);
					}
				}
			}
		}
		objectiveConstraint->RightHandSide = -10000;
		this->ObjFunct = originalObjective;
	}
	map<string,bool> koReactions;
	for (int i=0; i < int(InParameters->labels.size()); i++) {
		for (int j=0; j < int(InParameters->KOSets[i].size()); j++) {
			koReactions[InParameters->KOSets[i][j]] = true;
		}
	}
	map<string,bool> essentialGenes;
	if (GetParameter("Combinatorial deletions").compare("none") != 0) {
		vector<string>* strings = StringToStrings(GetParameter("Essential Gene List"),";");
		for (int i=0; i < int(strings->size()); i++) {
			essentialGenes[(*strings)[i]] = true;
		}
		delete strings;
	}
	this->LoadSolver(true);
	string lastmedia;
	int rerun = 0;
	map<string,bool> deletedReactions;
	for (int i=0; i < int(InParameters->labels.size()); i++) {
		cout << "Now simulating " << i << " of " << InParameters->labels.size() << endl;
		double WTgrowth = 0;
		double growth = 0;
		string fluxes("none");
		string KOrxn("none");
		string GeneKO("none");
		string noGrowth("NA");
		string label = InParameters->labels[i];
		string newInactiveReactions;
		string newEssentialGenes;
		string gapfilling("none");
		vector<Reaction*> KOReactions;
		vector<int> KODirections;
		if (deletedReactions.count(label) > 0) {
			newInactiveReactions = "DELETED";
		} else {
			//Loading media
			if (InParameters->mediaConditions[i].compare("NONE") != 0) {
				this->loadMedia(InParameters->mediaConditions[i],InData,true);
			}
			//Calculating WT growth
			this->ObjFunct = originalObjective;
			this->ObjectiveConstraint->RightHandSide = 0;
			LoadConstToSolver(this->ObjectiveConstraint->Index);
			this->SetMax();
			LoadObjective();
			NewSolution = RunSolver(false,true,false);
			if (NewSolution != NULL && NewSolution->Status == SUCCESS) {
				WTgrowth = NewSolution->Objective;
				if (WTgrowth < 1e-7) {
					if (GapfillPhenosim) {
						if (GetParameter("fit phenotype data").compare("0") == 0 || InParameters->observedgrowth.size() <= i || InParameters->observedgrowth[i] > 0) {
							for (int j=0; j < int(gapfillobj->Variables.size()); j++) {
								if (gapfillobj->Coefficient[j] > 0) {
									if (gapfillobj->Variables[j]->Type == FLUX || gapfillobj->Variables[j]->Type == FORWARD_FLUX || gapfillobj->Variables[j]->Type == REVERSE_FLUX) {
										gapfillobj->Variables[j]->Exclude = false;
									}
								}
							}
							if (this->Solver != CPLEX) {
								this->Solver = SOLVER_SCIP;
							}
							this->LoadSolver(true);
							NewSolution = RunSolver(false,false,false);
							if (NewSolution->Objective >= InParameters->MinimumTargetFlux) {
								this->ObjFunct = gapfillobj;
								this->ObjectiveConstraint->RightHandSide = InParameters->MinimumTargetFlux;
								LoadConstToSolver(this->ObjectiveConstraint->Index);
								this->SetMin();
								bool gapfilled = SolveGapfillingProblem(0,NewSolution,label,InParameters,"GapfillingOutput.txt");
								this->SetMax();
								cout << "Setting gapfilling:" << GetParameter("current gapfilled reactions") << endl;
								cout << "WTgrowth:" << WTgrowth << endl;
								gapfilling.assign(GetParameter("current gapfilled reactions"));
								for (int j=0; j < int(gapfillobj->Variables.size()); j++) {
									if (gapfillobj->Coefficient[j] > 0) {
										if (gapfillobj->Variables[j]->Type == FLUX || gapfillobj->Variables[j]->Type == FORWARD_FLUX || gapfillobj->Variables[j]->Type == REVERSE_FLUX) {
											gapfillobj->Variables[j]->Exclude = true;
										}
									}
								}
								this->LoadSolver(true);
							} else {
								gapfilling.assign("no solution found");
							}
							if (this->Solver != CPLEX) {
								this->Solver = GLPK;
							}
						} else {
							gapfilling.assign("not attempted");
						}
					} else {
						gapfilling.assign("not attempted");
					}
					growth = 0;
				} else {
					vector<Gene*> KOList;
					if (InParameters->KOSets[i].size() > 0 && ConvertToLower(InParameters->KOSets[i][0]).compare("none") != 0) {
						for (int j=0; j < int(InParameters->KOSets[i].size()); j++) {
							Gene* currentGene = InData->FindGene("DATABASE",InParameters->KOSets[i][j].data());
							if (currentGene != NULL) {
								KOList.push_back(currentGene);
							} else {
								Reaction* currentReaction = InData->FindReaction("DATABASE",InParameters->KOSets[i][j].data());
								if (currentReaction != NULL) {
									if (InParameters->KOSets[i][j].substr(0,1).compare("+") == 0) {
										KODirections.push_back(1);
									} else if (InParameters->KOSets[i][j].substr(0,1).compare("-") == 0) {
										KODirections.push_back(-1);
									} else {
										KODirections.push_back(0);
									}
									KOReactions.push_back(currentReaction);
								}
							}
						}
					}
					for (int j=0; j < int(KOList.size()); j++) {
						KOList[j]->SetMark(true);
					}
					for (int j=0; j < InData->FNumReactions(); j++) {
						if (InData->GetReaction(j)->CheckForKO()) {
							KOReactions.push_back(InData->GetReaction(j));
							KODirections.push_back(0);
						}
					}
					for (int j=0; j < int(KOReactions.size()); j++) {
						if (KOrxn.compare("none") == 0) {
							KOrxn = "";
						} else {
							KOrxn.append(";");
						}
						string prefix("");
						if (KODirections[j] == 1) {
							prefix.assign("+");
							KOReactions[j]->ResetFluxBounds(FLAG,0,this);
						} else if (KODirections[j] == -1) {
							prefix.assign("-");
							KOReactions[j]->ResetFluxBounds(0,FLAG,this);
						} else {
							KOReactions[j]->ResetFluxBounds(0,0,this);
						}
						KOrxn.append(prefix+KOReactions[j]->GetData("DATABASE",STRING));
					}
					for (int j=0; j < int(KOList.size()); j++) {
						KOList[j]->SetMark(false);
					}
					//Simulating KO if reactions are knocked out. Not simulating otherwise
					growth = WTgrowth;
					if (KOReactions.size() > 0) {
						NewSolution = RunSolver(false,true,false);
						growth = NewSolution->Objective;
					}
					if (NewSolution != NULL && NewSolution->Status == SUCCESS && growth > 1e-7) {
						if (GetParameter("Combinatorial deletions").compare("none") != 0) {
							if (CombinatorialKO(1,InData,false)) {
								vector<string>* strings = StringToStrings(GetParameter("Essential Gene List"),";");
								for (int i=0; i < int(strings->size()); i++) {
									if (essentialGenes.count((*strings)[i]) == 0) {
										if (newEssentialGenes.length() > 0) {
											newEssentialGenes.append(";");
										}
										newEssentialGenes.append((*strings)[i]);
									}
								}
								delete strings;
							}
						}
						//Finding tight bounds if this is also specified
						if (GetParameter("find tight bounds").compare("1") == 0) {
							objectiveConstraint->RightHandSide = NewSolution->Objective*0.1;
							this->LoadConstToSolver(objectiveConstraint->Index);
							bool currentSense = this->FMax();
							LinEquation* currObj = this->GetObjective();
							ObjFunct = NULL;
							this->FindTightBounds(InData,InParameters,false,true);
							bool nonContributing = true;
							for (int j=0; j < FNumVariables(); j++) {
								if ((GetVariable(j)->AssociatedReaction != NULL && GetVariable(j)->Type == FLUX) || (GetVariable(j)->AssociatedSpecies != NULL && GetVariable(j)->Type == DRAIN_FLUX && GetVariable(j)->Compartment == GetCompartment("c")->Index)) {
									if (fabs(GetVariable(j)->Max) < 1e-7 && fabs(GetVariable(j)->Min) < 1e-7) {
										string databaseID;
										if (GetVariable(j)->AssociatedReaction != NULL) {
											databaseID = GetVariable(j)->AssociatedReaction->GetData("DATABASE",STRING);
										} else {
											databaseID = GetVariable(j)->AssociatedSpecies->GetData("DATABASE",STRING);
											if (compoundsToAssess.count(databaseID) == 0) {
												databaseID = "";
											}
										}
										if (databaseID.length() > 0 && inactiveReactions.count(databaseID) == 0) {
											if (GetVariable(j)->AssociatedReaction != NULL && koReactions.count(databaseID) == 0) {
												nonContributing = false;
											}
											if (newInactiveReactions.length() > 0) {
												newInactiveReactions.append(";");
											}
											newInactiveReactions.append(databaseID);
										}
									}
								}
							}
							if (nonContributing && GetParameter("delete noncontributing reactions").compare("1") == 0) {
								deletedReactions[label] = true;
								newInactiveReactions.assign("DELETED");
								rerun = 1;
							}
							objectiveConstraint->RightHandSide = -10000;
							this->LoadConstToSolver(objectiveConstraint->Index);
							this->AddObjective(currObj);
							if (currentSense) {
								this->SetMax();
							} else {
								this->SetMin();
							}
							this->LoadObjective();
						}
						//Adding flux minimization if the user has asked to save fluxes
						if (InParameters->DoMinimizeFlux == 0 && GetParameter("save phenotype simulation fluxes").compare("1") == 0) {
							//For flux minimization, we force the current objective to a near maximal value
							ObjectiveConstraint->RightHandSide = NewSolution->Objective;
							LinEquation* CurrentObjective = this->ObjFunct;//Backing up old objective
							this->ObjFunct = NULL;
							this->AddSumObjective(FLUX,false,false,1,false);
							this->AddSumObjective(REVERSE_FLUX,false,true,1,false);
							this->AddSumObjective(FORWARD_FLUX,false,true,1,false);
							this->SetMin();
							if (this->Solver != CPLEX) {
								this->Solver = SOLVER_SCIP;
							}
							this->LoadSolver();
							OptSolutionData* CurrentSolution = RunSolver(true,true,true);
							CurrentSolution->Objective = CurrentOptimum;
							ObjFunct = CurrentObjective;
							if (this->FMax()) {
								this->SetMax();
							}
							LoadObjective();
						}
						fluxes = this->fluxToString();
					} else {
						growth = 0;
						if (NewSolution != NULL && NewSolution->Status == SUCCESS && InParameters->OptimizeMetabolitesWhenZero) {
							string note;
							CheckIndividualMetaboliteProduction(InData,InParameters,GetParameter("metabolites to optimize"),false,false,note,true);
							noGrowth = GetParameter("No growth metabolites");
							this->LoadSolver();
						}
					}
				}
			}
		}
		string newLine(label);
		newLine.append("\t");
		newLine.append(GeneKO);
		newLine.append("\t");
		newLine.append(KOrxn);
		newLine.append("\t");
		newLine.append(InParameters->mediaConditions[i]);
		newLine.append("\t");
		newLine.append(dtoa(WTgrowth));
		newLine.append("\t");
		newLine.append(dtoa(growth));
		newLine.append("\t");
		newLine.append(noGrowth);
		newLine.append("\t");
		newLine.append(newInactiveReactions);
		newLine.append("\t");
		newLine.append(newEssentialGenes);
		newLine.append("\t");
		newLine.append(gapfilling);
		if (GetParameter("save phenotype simulation fluxes").compare("1") == 0) {
			newLine.append("\t");
			newLine.append(fluxes);
		}
		cout << newLine << endl;
		outputVector.push_back(newLine);
		if (newInactiveReactions.compare("DELETED") == 0) {
			for (int j=0; j < int(KOReactions.size()); j++) {
				inactiveReactions[KOReactions[j]->GetData("DATABASE",STRING)] = true;
				MFAVariable* newVar = KOReactions[j]->GetMFAVar(FLUX);
				if (KODirections[j] == 1 || KODirections[j] == 0) {
					currentBounds->upperBounds[newVar->Index] = 0;
				} else if (KODirections[j] == -1 || KODirections[j] == 0) {
					currentBounds->lowerBounds[newVar->Index] = 0;
				}
			}
		}
		this->loadBounds(currentBounds,true);
		//if (GetParameter("delete noncontributing reactions").compare("1") == 0 && rerun == 1 && i == int(InParameters->labels.size()-1)) {
			//outputVector.clear();
			//SetParameter("delete noncontributing reactions","0");
			//i = -1;
			//rerun = 0;
		//}
	}
	delete currentBounds;
	//Reloading original bounds
	this->LoadSolver();
	this->loadBounds(originalBounds);
	delete originalBounds;
	//Printing results to file
	string outFile = FOutputFilepath()+"FBAExperimentOutput.txt";
	ofstream output(outFile.data());
	for (int i=0; i < int(outputVector.size()); i++) {
		output << outputVector[i] << endl;
	}
	output.close();
	return SUCCESS;
}	

int MFAProblem::RunMediaExperiments(Data* InData, OptimizationParameter* InParameters, double WildTypeObjective, bool DoOptimizeSingleObjective, bool DoFindTightBounds, bool MinimizeForeignReactions, bool OptimizeMetaboliteProduction) {
	bool OriginalPrint = InParameters->PrintSolutions;
	bool OriginalClear = InParameters->ClearSolutions;
	InParameters->PrintSolutions = false;
	InParameters->ClearSolutions = false;
	
	int Status = SUCCESS;
	string OriginalNote("Media experiment:");
	//First I build the problem from the model if it has not already been built
	if (FNumVariables() == 0) {
		if (BuildMFAProblem(InData,InParameters) != SUCCESS) {
			FErrorFile() << "Failed to build optimization problem." << endl;
			FlushErrorFile();
			OriginalNote.append("Failed to build optimization problem");
			PrintProblemReport(FLAG,InParameters,OriginalNote);
			return FAIL;	
		}
	}
	
	if (ConvertStringToObjective(GetParameter("objective"),InData) == NULL) {
		FErrorFile() << "Failed to create objective." << endl;
		FlushErrorFile();
		OriginalNote.append("Failed to create objective.");
		PrintProblemReport(FLAG,InParameters,OriginalNote);
		return FAIL;
	}

	//If there are integer variables, I relax them initially to speed up the calculations
	LinEquation* ObjectiveConstraint = NULL;
	vector<int> VariableTypes;
	VariableTypes.push_back(OBJECTIVE_TERMS);
	if (InParameters->ReactionsUse) {
		//RelaxIntegerVariables = true;
		if (DoOptimizeSingleObjective) {
			ObjectiveConstraint = ConvertStringToObjective(GetParameter("objective"), InData);
			ObjectiveConstraint = MakeObjectiveConstraint(-100,GREATER);
		}
	}
	
	//Now I load the solver once and only once to make the runs go faster
	LoadSolver();

	ifstream Input;
	//I read in the media file list
	string MFAInputFilename = GetDatabaseDirectory(true)+GetParameter("media list file");
	if (!OpenInput(Input,MFAInputFilename)) {
		return FAIL;
	}
	
	//Saving the original bounds so they can be restored when the analysis is complete
	vector<double> OriginalMax(FNumVariables());
	vector<double> OriginalMin(FNumVariables());
	for (int i=0; i < FNumVariables(); i++) {
		OriginalMax[i] = GetVariable(i)->UpperBound;
		OriginalMin[i] = GetVariable(i)->LowerBound;
	}
	int OriginalVariables = FNumVariables();
	
	//If the user requests it, all drain fluxes are cleared to the default values
	if (GetParameter("Clear all drain flux bounds before performing media study").compare("1") == 0) {
		this->clearOldMedia(InParameters);
	}

	//Saving the cleared bounds so they can be restored at each step of the analysis
	vector<double> PrestudyMax(FNumVariables());
	vector<double> PrestudyMin(FNumVariables());
	for (int i=0; i < FNumVariables(); i++) {
		PrestudyMax[i] = GetVariable(i)->UpperBound;
		PrestudyMin[i] = GetVariable(i)->LowerBound;
	}
	ofstream MediaStudyOutput;
	OpenOutput(MediaStudyOutput,FOutputFilepath()+"MediaStudyResults.txt");
	//Applying the bounds and performing MFA for each media
	do {
		//Reading in the filename for the next media file
		string Temp = GetFileLine(Input);
		string Filename = Temp+".txt";
		//Checking that the filename is valid
		if (Temp.length() > 0) {
			if (FileExists(GetDatabaseDirectory(true)+Filename)) {
				//Reloading the solver if it was reset during an analysis
				if (!FProblemLoaded()) {
					LoadSolver();
				}
				
				//Reading in the media file
				FileBounds* NewBounds = ReadBounds(Filename.data());
				
				//Saving the current variable bounds
				vector<double> CurrentMax(FNumVariables());
				vector<double> CurrentMin(FNumVariables());
				for (int i=0; i < OriginalVariables; i++) {
					CurrentMax[i] = GetVariable(i)->UpperBound;
					CurrentMin[i] = GetVariable(i)->LowerBound;
				}
				
				//Returning all variables to their prestudy bounds
				for (int i=0; i < OriginalVariables; i++) {
					GetVariable(i)->UpperBound = PrestudyMax[i];
					GetVariable(i)->LowerBound = PrestudyMin[i];
				}

				//Inputing the bounds for the new media
				ApplyInputBounds(NewBounds,InData);
				delete NewBounds;

				//Checking to see which variables have changed and loading changed variables to the solver
				for (int i=0; i < OriginalVariables; i++) {
					if (ProblemLoaded && (CurrentMax[i] != GetVariable(i)->UpperBound || CurrentMin[i] != GetVariable(i)->LowerBound)) {
						LoadVariable(i);
					}
				}
				
				//Modifying the note that will be printed to the problem report so this study result is labeled properly with the media used
				OriginalNote.assign("Media experiment: ");
				OriginalNote.append(Filename);
				
				double SingleObjectiveValue = 0;
				if (DoOptimizeSingleObjective) {
					Status = OptimizeSingleObjective(InData,InParameters,GetParameter("objective"),DoFindTightBounds,MinimizeForeignReactions,SingleObjectiveValue,OriginalNote);
					if (SingleObjectiveValue > 0.0000001) {
						MediaStudyOutput << Temp << "\tGROWTH\t" << SingleObjectiveValue << endl;
					} else {
						MediaStudyOutput << Temp << "\tNO GROWTH\t" << SingleObjectiveValue <<  endl;
					}
				}
				if (!DoOptimizeSingleObjective && DoFindTightBounds) {
					Status = FindTightBounds(InData,InParameters,OriginalNote);
				}
				if (OptimizeMetaboliteProduction) {
					Status = CheckIndividualMetaboliteProduction(InData,InParameters,GetParameter("metabolites to optimize"),false,false,OriginalNote,false);
				}
				if (GetParameter("run exploration experiments").compare("1") == 0) {
					ExploreSplittingRatios(InData,InParameters,DoFindTightBounds,MinimizeForeignReactions);	
				}
			} else {
				MediaStudyOutput << Temp << "\tNot found" << endl;
			}
		}
	} while(!Input.eof());
	MediaStudyOutput.close();

	//Restoring the original bounds and reloading all altered variables to the solver
	for (int i=0; i < OriginalVariables; i++) {
		if (OriginalMax[i] != GetVariable(i)->Max || OriginalMin[i] != GetVariable(i)->Min) {
			GetVariable(i)->Max = OriginalMax[i];
			GetVariable(i)->Min = OriginalMin[i];
		}
	}
	Status = LoadSolver(false);

	if (OriginalPrint) {
		PrintSolutions(-1,-1);
	}
	if (OriginalClear) {
		ClearSolutions();
	}
	InParameters->PrintSolutions = OriginalPrint;
	InParameters->ClearSolutions = OriginalClear;

	Input.close();

	return Status;
}

//This function loads the objective from the input string
int MFAProblem::OptimizeSingleObjective(Data* InData, OptimizationParameter* InParameters, string InObjective, bool FindTightBounds, bool MinimizeForeignReactions, double &ObjectiveValue, string Note) {
	//Reading in objective from parameters file
	SetMax();
	ObjectiveValue = 0;
	LinEquation* NewObjective = ConvertStringToObjective(InObjective, InData);
			
	if (NewObjective != NULL) {
		Note.append("Optimizing single objective:");
		Note.append(StringReplace(InObjective.data(),";",":"));

		if (GetParameter("Check potential constraints feasibility").compare("1") != 0) {
			return OptimizeSingleObjective(InData,InParameters,FindTightBounds,MinimizeForeignReactions,ObjectiveValue, Note,false);
		} else {
			return CheckPotentialConstraints(InData,InParameters,ObjectiveValue,Note);
		}
	}

	Note.append("Failed to convert string to objective");
	PrintProblemReport(FLAG,InParameters,Note);
	return FAIL;
}	

int MFAProblem::LoadAdditionalReactions(Data* InData,OptimizationParameter* InParameters) {
	if (InData->GetData("Reaction list loaded",STRING).length() == 0) {
		InData->AddData("Reaction list loaded","YES",STRING);
		vector< vector<string> >* rows = LoadMultipleColumnFile(FOutputFilepath()+"AdditionalReactions.txt","\t");
		for (int i=1; i < int(rows->size()); i++) {
			if (InData->FindReaction("DATABASE",(*rows)[i][0].data()) == NULL) {
				Reaction* NewReaction = new Reaction((*rows)[i][0].data(),InData);
				NewReaction->AddData("FOREIGN",(*rows)[i][2].data(),STRING);
				NewReaction->AddData("ORIGINALDIR",NewReaction->FType());
				if ((*rows)[i][1].compare("=") == 0) {
					NewReaction->SetType(REVERSIBLE);
				} else if ((*rows)[i][1].compare(">") == 0) {
					NewReaction->SetType(FORWARD);
				} else {
					NewReaction->SetType(REVERSE);
				}
				NewReaction = InData->AddReaction(NewReaction);
			}
		}
	}
	return SUCCESS;
}

int MFAProblem::BuildCoreProblem(Data* InData,OptimizationParameter*& InParameters) {
	//this->LoadBiomassDrainReactions(InData,InParameters);
	this->LoadAdditionalReactions(InData,InParameters);
	if (InParameters == NULL) {
		InParameters = ReadParameters();
	}
	InData->ResetAllBools(false,true,false,true,true,true,true,true);
	//Adding all variables except the use variables
	for (int i=0; i < InData->FNumReactions(); i++) {
		InData->GetReaction(i)->CreateMFAVariables(InParameters);
		InData->GetReaction(i)->GetAllMFAVariables(Variables);
	}
	for (int i=0; i < InData->FNumSpecies(); i++) {
		InData->GetSpecies(i)->CreateMFAVariables(InParameters);
		InData->GetSpecies(i)->GetAllMFAVariables(Variables);
	}
	ApplyInputBounds(InParameters->UserBounds,InData);
	//Enforcing the specified KO reactions
	for (int i=0; i < int(InParameters->KOReactions.size()); i++) {
		Reaction* Temp = InData->FindReaction("DATABASE",InParameters->KOReactions[i].data());
		if (Temp != NULL) {
			Temp->UpdateBounds(FLUX,0,0);
		}
	}
	for (int i=0; i < int(InParameters->KOGenes.size()); i++) {
		Gene* Temp = InData->FindGene("DATABASE",InParameters->KOGenes[i].data());
		if (Temp == NULL) {
			Temp = InData->AddGene(InParameters->KOGenes[i]);
		}
		if (Temp != NULL) {
			Temp->SetData("FBAKO",1);
			Temp->SetMark(true);
		}
	}
	for (int j=0; j < InData->FNumReactions(); j++) {
		if (InData->GetReaction(j)->CheckForKO()) {
			InData->GetReaction(j)->UpdateBounds(FLUX,0,0);
		}
	}
	if (InParameters->ThermoConstraints && !InParameters->SimpleThermoConstraints) {
		for (int i=0; i < InData->FNumStructuralCues(); i++) {
			if (InData->GetStructuralCue(i)->FEstDeltaG() == -10000) {
				AddVariable(InData->GetStructuralCue(i)->CreateMFAVariable(DELTAGG_ENERGY,-1,-1000,1000));
			}
		}
	}
	for (int i=0; i < InData->FNumReactions(); i++) {
		InData->GetReaction(i)->BuildReactionConstraints(InParameters,this);
	}
	for (int i=0; i < InData->FNumSpecies(); i++) {
		InData->GetSpecies(i)->BuildSpeciesConstraints(InParameters,this);
	}
	//Now that all of the necessary variables exist, I can read in and add the special constraints
	ApplyInputConstraints(InParameters->AddConstraints,InData);
	//Now I add mass balance constraints
	if (InParameters->MassBalanceConstraints) {
		AddMassBalanceConstraints(InData);
	}
	AddUptakeLimitConstraints();
	ConvertStringToObjective(GetParameter("objective"), InData);
	//Now I add mass balance constraints for particular atoms
	if (GetParameter("Mass balance atoms").length() > 0 && GetParameter("Mass balance atoms").compare("none") != 0) {
	  vector<string>* strings = StringToStrings(GetParameter("Mass balance atoms"),";");

	  for (int i=0; i < int(strings->size()); i++) {
	    	    AddMassBalanceAtomConstraint((*strings)[i].data(), InData);
	  }
	  for (int i=0; i < InData->FNumReactions(); i++) {
	  		string imbal = InData->GetReaction(i)->GetData("MASSIMBAL",STRING);
	  		if (imbal.length() > 0) {
	  			string message = "Reaction "+InData->GetReaction(i)->GetData("DATABASE",STRING)+" imbalanced\n"+imbal+"Reactants:\n";
	  			string products = "Products:\n";
	  			for (int j=0; j < InData->GetReaction(i)->FNumReactants(); j++) {
	  				Species* reactant = InData->GetReaction(i)->GetReactant(j);
	  				if (InData->GetReaction(i)->GetReactantCoef(j) < 0) {
	  					message.append("(");
	  					message.append(dtoa(-1*InData->GetReaction(i)->GetReactantCoef(j)));
	  					message.append(") ");
	  					message.append(reactant->GetData("NAME",STRING,1));
	  					message.append(" [");
	  					message.append(reactant->GetData("DATABASE",STRING));
	  					message.append("] ");
	  					message.append(reactant->FFormula());
	  					message.append("\n");
	  				} else {
	  					products.append("(");
	  					products.append(dtoa(InData->GetReaction(i)->GetReactantCoef(j)));
	  					products.append(") ");
	  					products.append(reactant->GetData("NAME",STRING,1));
	  					products.append(" [");
	  					products.append(reactant->GetData("DATABASE",STRING));
	  					products.append("] ");
	  					products.append(reactant->FFormula());
						products.append("\n");
	  				}
	  			}
	  			message += products;
	  			MFALog->push_back(message);
	  		}
	  }
	}
	//	AddDeltaGofFormationConstraint(InData);
}

int MFAProblem::AddMassBalanceAtomConstraint(const char* ID, Data* InData) {
  // two inequalities to constrain absolute value
  LinEquation* newConstraint = InitializeLinEquation("Mass balance constraint",MFA_ZERO_TOLERANCE,LESS);
  LinEquation* newConstraintReciprocal = InitializeLinEquation("Mass balance constraint",MFA_ZERO_TOLERANCE,LESS);
  // start with drain variables; 
  for (int j=0; j < this->FNumVariables();j++) {
    MFAVariable* currVar = this->GetVariable(j);
    if ((currVar->Type == FORWARD_DRAIN_FLUX || currVar->Type == DRAIN_FLUX) && currVar->AssociatedSpecies != NULL) {
      int atomCount = currVar->AssociatedSpecies->CountAtomType(ID);
      cout << "(1) pushing " << atomCount << " for " << currVar->AssociatedSpecies->GetData("DATABASE",STRING) << endl;
      newConstraint->Variables.push_back(currVar);
      newConstraint->Coefficient.push_back(atomCount);
      newConstraintReciprocal->Variables.push_back(currVar);
      newConstraintReciprocal->Coefficient.push_back(-atomCount);
    }
    if ((currVar->Type == REVERSE_DRAIN_FLUX) && currVar->AssociatedSpecies != NULL) {
      int atomCount = currVar->AssociatedSpecies->CountAtomType(ID);
      cout << "(2) pushing " << atomCount << " for " << currVar->AssociatedSpecies->GetData("DATABASE",STRING) << endl;
      newConstraint->Variables.push_back(currVar);
      newConstraint->Coefficient.push_back(-atomCount);
      newConstraintReciprocal->Variables.push_back(currVar);
      newConstraintReciprocal->Coefficient.push_back(atomCount);
    }
  }

  // include all reactants and products of the biomass reaction
  for (int i=0; i < InData->FNumReactions(); i++) {
    Reaction* reaction = InData->GetReaction(i);
    double biomass = 0;
    if (reaction->GetData("DATABASE",STRING).length() > 3 && reaction->GetData("DATABASE",STRING).substr(0,3).compare("bio") == 0) {
      MFAVariable* currVar = reaction->GetMFAVar(FLUX);
      if (currVar == NULL) {
	currVar = reaction->GetMFAVar(FORWARD_FLUX);
      }
      for (int j=0; j < reaction->FNumReactants(); j++) {
	Species* reactant = reaction->GetReactant(j);
	int atomCount = reactant->CountAtomType(ID);
	double flux = atomCount * reaction->GetReactantCoef(j);
	cout << "(3) pushing " << flux << " for " << reactant->GetData("DATABASE",STRING) << endl;
	biomass += flux;
      }
      newConstraint->Variables.push_back(currVar);
      newConstraint->Coefficient.push_back(biomass);
      newConstraintReciprocal->Variables.push_back(currVar);
      newConstraintReciprocal->Coefficient.push_back(-biomass);
    }
  }

  this->AddConstraint(newConstraint);
  this->AddConstraint(newConstraintReciprocal);

  // shut down any mass imbalanced reaction
  for (int i=0; i < InData->FNumReactions(); i++) {
    Reaction* reaction = InData->GetReaction(i);
    if (reaction->GetData("DATABASE",STRING).length() > 3 && reaction->GetData("DATABASE",STRING).substr(0,3).compare("bio") == 0) {
      continue; // skip biomass reaction which is by definition balanced since cpd11416 has no formula
    }
    float numID = 0;
    for (int j=0; j < reaction->FNumReactants(); j++) {
      Species* reactant = reaction->GetReactant(j);
      numID += reactant->CountAtomType(ID)*reaction->GetReactantCoef(j);
    }
    if (numID != 0) {
    	string massimbal = reaction->GetData("MASSIMBAL",STRING);
    	if (numID > 0) {
    		massimbal.append("Extra ");
    		massimbal.append(dtoa(numID));
    		massimbal.append(" ");
    		massimbal.append(ID);
    		massimbal.append(" in products\n");
    	} else {
    		massimbal.append("Extra ");
			massimbal.append(dtoa(-1*numID));
			massimbal.append(" ");
			massimbal.append(ID);
			massimbal.append(" in reactants\n");
    	}
    	reaction->SetData("MASSIMBAL",massimbal.data(),STRING);

      if (reaction->GetMFAVar(FLUX) != NULL) {
	LinEquation* shutdown = InitializeLinEquation("Shut down flux",0,EQUAL);
	shutdown->Variables.push_back(reaction->GetMFAVar(FLUX));
	shutdown->Coefficient.push_back(1);
	this->AddConstraint(shutdown);
      }
      if (reaction->GetMFAVar(FORWARD_FLUX) != NULL) {
	LinEquation* shutdown = InitializeLinEquation("Shut down flux",0,EQUAL);
	shutdown->Variables.push_back(reaction->GetMFAVar(FORWARD_FLUX));
	shutdown->Coefficient.push_back(1);
	this->AddConstraint(shutdown);
      }
      if (reaction->GetMFAVar(REVERSE_FLUX) != NULL) {
	LinEquation* shutdown = InitializeLinEquation("Shut down flux",0,EQUAL);
	shutdown->Variables.push_back(reaction->GetMFAVar(REVERSE_FLUX));
	shutdown->Coefficient.push_back(1);
	this->AddConstraint(shutdown);
      }
    }
  }  

  return SUCCESS;
}

// formation energy of products leaving the system minus reactants entering the system should be less than zero
int MFAProblem::AddDeltaGofFormationConstraint(Data* InData) {
  LinEquation* newConstraint = InitializeLinEquation("Mass balance constraint",0,LESS);
  // start with drain variables; 
  for (int j=0; j < this->FNumVariables();j++) {
    MFAVariable* currVar = this->GetVariable(j);
    if (currVar->AssociatedSpecies != NULL && currVar->AssociatedSpecies->GetData("DATABASE",STRING).compare("cpd11416_c0") == 0) {
      continue;
    }
    if ((currVar->Type == FORWARD_DRAIN_FLUX || currVar->Type == DRAIN_FLUX) && currVar->AssociatedSpecies != NULL) {
      double deltaG = currVar->AssociatedSpecies->FEstDeltaG();
      cout << "(1) pushing deltaG " << deltaG << " for " << currVar->AssociatedSpecies->GetData("DATABASE",STRING) << endl;
      newConstraint->Variables.push_back(currVar);
      newConstraint->Coefficient.push_back(-deltaG); // negate, because positive DRAIN_FLUX means reactant is entering the system
    }
    if ((currVar->Type == REVERSE_DRAIN_FLUX) && currVar->AssociatedSpecies != NULL) {
      double deltaG = currVar->AssociatedSpecies->FEstDeltaG();
      cout << "(2) pushing deltaG " << deltaG << " for " << currVar->AssociatedSpecies->GetData("DATABASE",STRING) << endl;
      newConstraint->Variables.push_back(currVar);
      newConstraint->Coefficient.push_back(deltaG);
    }
  }

  // include all reactants and products of the biomass reaction
  for (int i=0; i < InData->FNumReactions(); i++) {
    Reaction* reaction = InData->GetReaction(i);
    double biomass = 0;
    if (reaction->GetData("DATABASE",STRING).length() > 3 && reaction->GetData("DATABASE",STRING).substr(0,3).compare("bio") == 0) {
      MFAVariable* currVar = reaction->GetMFAVar(FLUX);
      if (currVar == NULL) {
	currVar = reaction->GetMFAVar(FORWARD_FLUX);
      }
      for (int j=0; j < reaction->FNumReactants(); j++) {
	Species* reactant = reaction->GetReactant(j);
	if (reactant->GetData("DATABASE",STRING).compare("cpd11416_c0") == 0) {
	  continue;
	}
	double deltaG = reactant->FEstDeltaG();
	double flux = deltaG * reaction->GetReactantCoef(j);
	cout << "(3) pushing deltaG " << deltaG << " for biomass: " << reactant->GetData("DATABASE",STRING) << endl;
	biomass += flux;
      }
      newConstraint->Variables.push_back(currVar); 
      newConstraint->Coefficient.push_back(-biomass); // negate, because reactant coefficients are negative
    }
  }

  this->AddConstraint(newConstraint);

  return SUCCESS;
}

int MFAProblem::AddUptakeLimitConstraints() {
	//Adding atom uptake constraints
	if (GetParameter("uptake limits").compare("none") != 0) {
		vector<string>* strings = StringToStrings(GetParameter("uptake limits"),";");
		for (int i=0; i < int(strings->size()); i++) {
			vector<string>* stringsTwo = StringToStrings((*strings)[i],":");
			if (stringsTwo->size() >= 2) {
				LinEquation* newConstraint = InitializeLinEquation("Uptake constraint",atof((*stringsTwo)[1].data()),LESS);
				for (int j=0; j < this->FNumVariables();j++) {
					MFAVariable* currVar = this->GetVariable(j);
					if ((currVar->Type == FORWARD_DRAIN_FLUX || currVar->Type == DRAIN_FLUX) && currVar->AssociatedSpecies != NULL) {
						int atomCount = currVar->AssociatedSpecies->CountAtomType((*stringsTwo)[0].data());
						newConstraint->Variables.push_back(currVar);
						newConstraint->Coefficient.push_back(atomCount);
					}
				}
				this->AddConstraint(newConstraint);
			}
			delete stringsTwo;
		}
		delete strings;
	}
	return SUCCESS;
}

int MFAProblem::AddPROMConstraints(Data* InData, OptimizationParameter* InParameters,OptSolutionData*& CurrentSolution) {
	InData->LoadPROMModel(GetParameter("PROM model filename"));//Loads PROM model data from file
	ObjectiveConstraint->RightHandSide = -10000;
	LoadConstToSolver(ObjectiveConstraint->Index);
	if (MinFluxConstraint != NULL) {
		MinFluxConstraint->RightHandSide = 1.1*MinFluxConstraint->RightHandSide;
	}
	MFAVariable* alpha = InitializeMFAVariable();
	alpha->Name.assign("alpha");
	alpha->Type = REACTION_CONSTRAINT;
	alpha->LowerBound = 0;
	alpha->UpperBound = 0;
	LoadVariable(AddVariable(alpha));
	MFAVariable* beta = InitializeMFAVariable();
	beta->Name.assign("beta");
	beta->Type = REACTION_CONSTRAINT;
	beta->LowerBound = 0;
	beta->UpperBound = 0;
	LoadVariable(AddVariable(beta));
	for (int i=0; i < FNumVariables(); i++) {
		if (GetVariable(i)->Type == FLUX || GetVariable(i)->Type == FORWARD_FLUX || GetVariable(i)->Type == REVERSE_FLUX) {
			double promact = GetVariable(i)->AssociatedReaction->ComputePROMActivity();
			cout << GetVariable(i)->AssociatedReaction->GetData("DATABASE",STRING) << "\t" << promact << endl;
			if (promact != 1) {
				LinEquation* NewConstraint = InitializeLinEquation();
				if (GetVariable(i)->Min < 0) {
					NewConstraint->RightHandSide = promact*1.01*GetVariable(i)->Min;
				} else {
					NewConstraint->RightHandSide = 0;
				}
				NewConstraint->EqualityType =GREATER;
				NewConstraint->ConstraintType = LINEAR;
				NewConstraint->ConstraintMeaning.assign("PROM min constraint");
				NewConstraint->Coefficient.push_back(1);
				NewConstraint->Coefficient.push_back(1);
				NewConstraint->Variables.push_back(GetVariable(i));
				NewConstraint->Variables.push_back(alpha);
				NewConstraint->AssociatedReaction = GetVariable(i)->AssociatedReaction;
				NewConstraint->AssociatedSpecies = GetVariable(i)->AssociatedReaction->GetReactant(i);
				LoadConstToSolver(AddConstraint(NewConstraint));
				LinEquation* NewConstraint2 = InitializeLinEquation();
				if (GetVariable(i)->Max < 0) {
					NewConstraint2->RightHandSide = 0;
				} else {
					if (GetVariable(i)->Max == 0) {
						GetVariable(i)->Max = 100;
					}
					NewConstraint2->RightHandSide = promact*1.01*GetVariable(i)->Max;
				}
				NewConstraint2->EqualityType =LESS;
				NewConstraint->ConstraintType = LINEAR;
				NewConstraint2->ConstraintMeaning.assign("PROM max constraint");
				NewConstraint2->Coefficient.push_back(1);
				NewConstraint2->Coefficient.push_back(-1);
				NewConstraint2->Variables.push_back(GetVariable(i));
				NewConstraint2->Variables.push_back(beta);
				NewConstraint2->AssociatedReaction = GetVariable(i)->AssociatedReaction;
				NewConstraint2->AssociatedSpecies = GetVariable(i)->AssociatedReaction->GetReactant(i);
				LoadConstToSolver(AddConstraint(NewConstraint2));
			}
		}
	}
	double Kappa = atof(GetParameter("PROM Kappa").data());
	if (FMax()) {
	  Kappa = -1 * Kappa; // because objective is max.
	}
	LinEquation* NewObjective = CloneLinEquation(GetObjective());
	NewObjective->Variables.push_back(alpha);
	NewObjective->Variables.push_back(beta);
	NewObjective->Coefficient.push_back(Kappa);
	NewObjective->Coefficient.push_back(Kappa);
	AddObjective(NewObjective);
	LoadObjective();
	this->ResetSolver();
	LoadSolver();
	this->WriteLPFile();
	CurrentSolution = RunSolver(true,true,true);
	CurrentSolution->Objective = 0;
	for (int i=0; i < int(ObjectiveConstraint->Variables.size()); i++) {
		CurrentSolution->Objective += ObjectiveConstraint->Coefficient[i]*CurrentSolution->SolutionData[ObjectiveConstraint->Variables[i]->Index];
	}
	alpha->UpperBound = CurrentSolution->SolutionData[alpha->Index];
	alpha->LowerBound = CurrentSolution->SolutionData[alpha->Index];
	LoadVariable(alpha->Index);
	beta->UpperBound = CurrentSolution->SolutionData[beta->Index];
	beta->LowerBound = CurrentSolution->SolutionData[beta->Index];
	LoadVariable(beta->Index);
	ObjectiveConstraint->RightHandSide = InParameters->OptimalObjectiveFraction*CurrentSolution->Objective;//Fixing objective at maximum value
	LoadConstToSolver(ObjectiveConstraint->Index);
}

//This function determines the minimal additions to the media that must be made in order for growth to occur
int MFAProblem::DetermineMinimalFeasibleMedia(Data* InData,OptimizationParameter* InParameters,OptSolutionData*& CurrentSolution,bool augment_only) {
	//Saving the old objective
	SavedBounds* bounds = this->saveBounds();
	LinEquation* OldObjective = GetObjective();
	ObjFunct = NULL;
	bool originalsense = this->FMax();
	//Relaxing min flux constraint
	double original_min_flux;
	if (MinFluxConstraint != NULL) {
		original_min_flux = MinFluxConstraint->RightHandSide;
		MinFluxConstraint->RightHandSide = 100000;
		LoadConstToSolver(MinFluxConstraint->Index);
	}
	//Setting objective to minimize drain fluxes or drain use variables
	this->SetMin();
	ClearObjective(true);
	ObjFunct = InitializeLinEquation();
	ObjFunct->ConstraintType = LINEAR;
	if (InParameters->DrainUseVar) {
		for (int i=0; i < FNumVariables(); i++) {
			if (GetVariable(i)->Type == DRAIN_USE || GetVariable(i)->Type == FORWARD_DRAIN_USE) {
				if (!augment_only || GetVariable(i)->UpperBound < MFA_ZERO_TOLERANCE) {
					ObjFunct->Coefficient.push_back(1);
					ObjFunct->Variables.push_back(GetVariable(i));
					if (augment_only) {
						GetVariable(i)->UpperBound = 100;
					}
				}
			}
		}
	} else {
		for (int i=0; i < FNumVariables(); i++) {
			if (GetVariable(i)->Type == DRAIN_FLUX || GetVariable(i)->Type == FORWARD_DRAIN_FLUX) {
				if (!augment_only || GetVariable(i)->UpperBound < MFA_ZERO_TOLERANCE) {
					ObjFunct->Coefficient.push_back(1);
					ObjFunct->Variables.push_back(GetVariable(i));
					if (augment_only) {
						GetVariable(i)->UpperBound = 100;
					}
				}
			}
		}
	}
	int Status = LoadObjective();
	ofstream output;
	if (OpenOutput(output,(FOutputFilepath()+"MinimalMediaOutput.txt").data())) {
		output << "Label\tObjective\tMedia objective\tRR score/count\tAR score/count\tRJ score/count\tCC score/count\tReactions retained\tActivated reactions\tRejected reactions\tCandidates cut\n";
		output.close();
	}
	SolveGapfillingProblem(1,CurrentSolution,"MinimalMedia",InParameters,"MinimalMediaOutput.txt");
	//Restoring original problem
	if (MinFluxConstraint != NULL) {
		MinFluxConstraint->RightHandSide = original_min_flux;
	}
	delete ObjFunct;
	if (originalsense) {
		this->SetMax();
	}
	ObjFunct = OldObjective;
	this->loadBounds(bounds,true);
	//Forcing minimal media formulation
	for (int i=0; i < FNumVariables(); i++) {
		if (GetVariable(i)->Type == DRAIN_FLUX || GetVariable(i)->Type == FORWARD_DRAIN_FLUX) {
			if (CurrentSolution->SolutionData[GetVariable(i)->Index] <= MFA_ZERO_TOLERANCE) {
				GetVariable(i)->UpperBound = 0;
			}
		}
	}
	return SUCCESS;
}

//This is a new function for FBA attempting to better integrate all the various analyses we do
int MFAProblem::FluxBalanceAnalysisMasterPipeline(Data* InData, OptimizationParameter* InParameters) {
	//Clearing any existing problem
	if (Variables.size() > 0 || Constraints.size() > 0) {
		ClearObjective();
		ClearConstraints();
		ClearVariables();
	}
	//Building core problem
	BuildCoreProblem(InData,InParameters);
	if (this->Solver != CPLEX) {
		this->Solver = SOLVER_SCIP;
	}
	this->LoadSolver(false);
	bool rerun = false;
	OptSolutionData* CurrentSolution = RunSolver(true,true,true);
	if (CurrentSolution == NULL || CurrentSolution->Status != SUCCESS) {
		return FAIL;
	}
	//If initial objective is zero, we conduct appropriate followup studies
	vector<MFAVariable*> activated;
	if (fabs(CurrentSolution->Objective) < MFA_ZERO_TOLERANCE) {
		if (InParameters->OptimizeMediaWhenZero) {
			DetermineMinimalFeasibleMedia(InData,InParameters,CurrentSolution,true);
		}
		activated = this->BiomassSensitivityAnalysis(CurrentSolution,InParameters);
		ofstream output;
		if(OpenOutput(output,(FOutputFilepath()+"BiomassRemovals.txt").data(),false)) {
			for (int i=0; i < int(activated.size()); i++) {
				output << activated[i]->AssociatedReaction->GetData("DATABASE",STRING) << endl;
			}
			output.close();
		}
		if (!InParameters->TranscriptomeAnalysis && !InParameters->GapFilling) {
			return FAIL;
		}
	}
	CurrentOptimum = CurrentSolution->Objective;
	//Fixing objective at max
	int sense = GREATER;
	if (!FMax()) {
		sense = LESS;
	}
	float originalrhs = CurrentSolution->Objective;
	if (ObjectiveConstraint == NULL) {
		ObjectiveConstraint = MakeObjectiveConstraint(InParameters->OptimalObjectiveFraction*CurrentSolution->Objective,sense);
	}
	ObjectiveConstraint->RightHandSide = InParameters->OptimalObjectiveFraction*CurrentSolution->Objective;
	LoadConstToSolver(ObjectiveConstraint->Index);
	LinEquation* CurrentObjective = ObjFunct;
	if (InParameters->PROM) {
		this->AddPROMConstraints(InData,InParameters,CurrentSolution);//Add the PROM constraints tightening bounds based on TF status
	}
	if (InParameters->TranscriptomeAnalysis || InParameters->GapFilling) {
		this->GapFilling(InData,InParameters,CurrentSolution);//testing
	}

	if (InParameters->DetermineMinimalMedia) {
		this->DetermineMinimalFeasibleMedia(InData,InParameters,CurrentSolution,false);//working
		this->LoadSolver();
		CurrentSolution = RunSolver(true,true,true);
		cout << "Old optimum:" << CurrentOptimum << endl;
		CurrentOptimum = CurrentSolution->Objective;
		cout << "New optimum:" << CurrentOptimum << endl;
	}

	//If flux variability analysis is selected, we run it now
	if (InParameters->FluxVariabilityAnalysis) {
		//We will never really want to run FVA with integer vairables - it will just be too slow
		CurrentObjective = ObjFunct;
		this->RelaxIntegerVariables = true;
		ObjectiveConstraint->RightHandSide = 0.1*CurrentOptimum;
		if (this->Solver != CPLEX) {
			this->Solver = GLPK;
		}
		this->LoadSolver();
		ObjFunct = NULL;
		this->FindTightBounds(InData,InParameters,false,true);
		this->RelaxIntegerVariables = false;
		ObjFunct = CurrentObjective;
		this->LoadSolver();
	}

	//In this optional analysis, we maximize the number of reactions carrying flux at the same time
	int BeforeActivationState = this->SaveState();
	if (InParameters->MaximizeActiveReactions) {
		this->ObjFunct = NULL;
		this->AddSumObjective(REACTION_SLACK,false,false,1,false);
		this->SetMin();
		if (this->Solver != CPLEX) {
			this->Solver = SOLVER_SCIP;
		}
		this->LoadSolver();
		CurrentSolution = RunSolver(true,true,true);
		CurrentSolution->Objective = CurrentOptimum;
		//Forcing all still inactive reactions to remain inactive
		for (int i=0; i < int(this->ObjFunct->Variables.size()); i++) {
			if (CurrentSolution->SolutionData[this->ObjFunct->Variables[i]->Index] <= MFA_ZERO_TOLERANCE) {
				this->ObjFunct->Variables[i]->UpperBound = 0;
				this->ObjFunct->Variables[i]->LowerBound = 0;
				this->LoadVariable(this->ObjFunct->Variables[i]->Index);
			}
		}
		//Restoring the old objective
		ObjFunct = CurrentObjective;
		if (sense == GREATER) {
			this->SetMax();
		}
		LoadObjective();
	}

	//In this optional analysis, we minimize the sum of the fluxes through all active reactions
	if (InParameters->DoMinimizeFlux) {
		//For flux minimization, we force the current objective to a near maximal value
		ObjectiveConstraint->RightHandSide = CurrentOptimum;
		CurrentObjective = ObjFunct;//Backing up old objective
		ObjFunct = NULL;
		this->AddSumObjective(FLUX,false,false,1,false);
		this->AddSumObjective(REVERSE_FLUX,false,true,1,false);
		this->AddSumObjective(FORWARD_FLUX,false,true,1,false);
		this->SetMin();
		if (this->Solver != CPLEX) {
			this->Solver = SOLVER_SCIP;
		}
		this->LoadSolver();
		CurrentSolution = RunSolver(true,true,true);
		CurrentSolution->Objective = CurrentOptimum;
 		ObjFunct = CurrentObjective;
 		if (sense == GREATER) {
 			this->SetMax();
 		}
 		int Status = LoadObjective();
	}
	PrintSolutions(FNumSolutions()-1,FNumSolutions());

	//Restoring bounds on variables fixed by the maximize active reactions analysis
	this->LoadState(BeforeActivationState,true,false,false,false,false);
	if (InParameters->ReactionKOSensitivityAnalysis) {
		this->ReactionSensitivityAnalysis(InData,CurrentSolution,InParameters);
	} else if (InParameters->GapGeneration) {
		this->GapGeneration(InData,InParameters);//TODO
	} else if (InParameters->QuantitativeOptimization) {
		this->QuantitativeModelOptimization(InData,InParameters);
	} else if (InParameters->PhenotypeAnalysis && !InParameters->GapFilling) {
		ObjectiveConstraint->RightHandSide = 0;
		LoadConstToSolver(ObjectiveConstraint->Index);
		this->RunDeletionExperiments(InData,InParameters,false);//TODO
	} else if (InParameters->PerformSingleKO) {
		ObjectiveConstraint->RightHandSide = 0.1*CurrentOptimum;
		LoadConstToSolver(ObjectiveConstraint->Index);
		this->CombinatorialKO(1,InData);//working
	} else if (InParameters->DoCalculateSensitivity) {
		this->CalculateFluxSensitivity(InData,Variables,CurrentSolution->Objective);//working
	} else if (InParameters->DoFluxCouplingAnalysis) {
		string note;
		this->FluxCouplingAnalysis(InData,InParameters,false,note,false);//TODO
	}
	//PrintSolutions(-1,-1);//working
}

//This function assumes that the desired objective function has already been loaded
int MFAProblem::OptimizeSingleObjective(Data* InData, OptimizationParameter* InParameters, bool DoFindTightBounds, bool MinimizeForeignReactions, double &ObjectiveValue, string Note, bool SubProblem) {
	int Status = SUCCESS;
	ObjectiveValue = 0;

	//Saving the original variable relaxation settings
	bool OriginalRelaxation = RelaxIntegerVariables;
	//This boolean indicates whether or not the integer variables can be relaxed
	bool RelaxationPossible = false;

	//Building the problem from the model if it has not already been built
	if (FNumVariables() == 0) {
		if (BuildMFAProblem(InData,InParameters) != SUCCESS) {
			FErrorFile() << "Failed to build optimization problem." << endl;
			FlushErrorFile();
			Note.append("Failed to build optimization problem");
			PrintProblemReport(FLAG,InParameters,Note);
			return FAIL;	
		}
	}
	
	//Clearing the solutions buffer if this is not a subproblem
	bool DoFluxCouplingAnalysis = false;
	bool DoMILPCoessentiality = false;
	bool DoMinimizeReactions = false;
	bool DoIntervalOptimization = false;
	bool DoGeneOptimization = false;
	bool DoMinimizeDeltaGError = false;
	bool DoMinimizeFlux = false;
	if (!SubProblem) {
		DoMinimizeFlux = InParameters->DoMinimizeFlux;
		DoFluxCouplingAnalysis = InParameters->DoFluxCouplingAnalysis;
		DoMILPCoessentiality = InParameters->DoMILPCoessentiality;
		DoMinimizeReactions = InParameters->DoMinimizeReactions;
		DoIntervalOptimization = InParameters->IntervalOptimization;
		DoGeneOptimization = InParameters->GeneOptimization;
		DoMinimizeDeltaGError = InParameters->MinimizeDeltaGError;
		if (InParameters->ClearSolutions) {
			ClearSolutions();
		}
	}

	//Checking to ensure that the problem has an objective
	OptSolutionData* NewSolution = NULL;
	if (GetObjective() == NULL) {
		Note.append("Problem does not have objective");
		PrintProblemReport(FLAG,InParameters,Note);
		return FAIL;
	}

	//Relaxing integer variables if (i) the user allows it and (ii) the objective involves no integers and (iii) thermodynamic constraints are not in use
	if (InParameters->RelaxIntegerVariables && !InParameters->ThermoConstraints) {
		RelaxIntegerVariables = false;
		//Checking to see if there are any integer variables
		for (int i=0; i < FNumVariables(); i++) {
			if (GetVariable(i)->Binary || GetVariable(i)->Integer) {
				RelaxIntegerVariables = true;
				RelaxationPossible = true;
			}
		}
		
		//Checking to see if there are any integer variables in the objective function
		for (int i=0; i < int(ObjFunct->Variables.size()); i++) {
			if (ObjFunct->Variables[i]->Binary || ObjFunct->Variables[i]->Integer) {
				RelaxationPossible = true;
				RelaxIntegerVariables = false;
			}
		}
		
		//Reseting the solver if we need to relax the integer variables
		if (RelaxIntegerVariables && !OriginalRelaxation) {
			ResetSolver();
		}
	}

	//Loading the problem data into the solver if it is not already loaded... this automatically resets the solver
	if (ProblemLoaded) {
		Status = LoadObjective();
	} else {
		Status = LoadSolver(true);
	}
	if (Status != SUCCESS) {
		Note.append("Problem failed to load into solver");
		PrintProblemReport(FLAG,InParameters,Note);
		return FAIL;
	}

	//Modifying loaded constraints if specified by user before any analysis
	ModifyInputConstraints(InParameters->ModConstraints,InData);

	//Printing the LP file rather than solving
	if (GetParameter("just print LP file").compare("1") == 0) {
		PrintVariableKey();
		WriteLPFile();
		return SUCCESS;
	}

	//Running the solver and obtaining and checking the solution returned.
	NewSolution = RunSolver(true,true,true);
	if (NewSolution == NULL) {
		Note.append("Problem failed to return a solution");
		PrintProblemReport(FLAG,InParameters,Note);
		return FAIL;
	} else if (NewSolution->Status != SUCCESS) {
		Note.append("Returned solution is infeasible");
		PrintProblemReport(FLAG,InParameters,Note);
		return FAIL;
	}
	ObjectiveValue = NewSolution->Objective;
	NewSolution->Notes.assign(Note);
	if (!SubProblem && GetParameter("calculate flux sensitivity").compare("1") == 0) {
		this->InputSolution(NewSolution);
		this->CalculateFluxSensitivity(InData,this->Variables,NewSolution->Objective);
	}
	if (ObjectiveValue > MFA_ZERO_TOLERANCE && !SubProblem && GetParameter("Combinatorial deletions").compare("none") != 0) {
		CombinatorialKO(atoi(GetParameter("Combinatorial deletions").data()),InData,false);
	}
	if (ObjectiveValue > MFA_ZERO_TOLERANCE && !SubProblem && GetParameter("Combinatorial reaction deletions").compare("none") != 0) {
		CombinatorialKO(atoi(GetParameter("Combinatorial reaction deletions").data()),InData,true);
	}
	if (!SubProblem && InParameters->KOSets.size() > 0) {
		RunDeletionExperiments(InData,InParameters);
	}
	//Checking where the tightbounds are invalid
	for (int i=0; i < FNumVariables(); i++) {
		if (i < int(NewSolution->SolutionData.size())) {
			if (NewSolution->SolutionData[GetVariable(i)->Index] < GetVariable(i)->Min && GetVariable(i)->Min != FLAG) {
				//FLogFile() << "Violation for variable " << i << ", min: " << GetVariable(i)->Min << ", value: " << NewSolution->SolutionData[GetVariable(i)->Index] << endl;
			}
			if (NewSolution->SolutionData[GetVariable(i)->Index] > GetVariable(i)->Max && GetVariable(i)->Max != FLAG) {
				//FLogFile() << "Violation for variable " << i << ", max: " << GetVariable(i)->Max << ", value: " << NewSolution->SolutionData[GetVariable(i)->Index] << endl;
			}
		}
	}
	//If growth was observed, we determine what media could be removed to lose growth
	if (ObjectiveValue > MFA_ZERO_TOLERANCE && GetParameter("find essential media").compare("1") == 0) {
		vector<MFAVariable*> CurrentKO;
		vector<MFAVariable*> NonessentialMedia;
		string Result = MediaSensitivityExperiment(InData,InParameters,CurrentKO,NonessentialMedia);
		SetParameter("Essential media",Result.data());
		if (Result.length() > 0) {
			Note.append("|EssentialMedia:"+Result);
		}
	}
	//Building and testing the dual problem
	if (fabs(ObjectiveValue) > MFA_ZERO_TOLERANCE && GetParameter("Build dual problem").compare("1") == 0 && !SubProblem) {
		ResetSolver();
		MFAProblem* NewProblem = new MFAProblem();
		NewProblem->BuildDualMFAProblem(this,InData,InParameters);
		NewProblem->LoadSolver(true);
		OptSolutionData* NewSolution = RunSolver(true,true,true);
		if (NewSolution == NULL) {
			Note.append("Problem failed to return a solution");
			PrintProblemReport(FLAG,InParameters,Note);
			return FAIL;
		} else if (NewSolution->Status != SUCCESS) {
			Note.append("Returned solution is infeasible");
			PrintProblemReport(FLAG,InParameters,Note);
			return FAIL;
		} else {
			cout << "Dual objective: " << NewSolution->Objective << endl;
		}
		ResetSolver();
		ResetIndecies();
	}
	//Checking individual reaction essentiality if requested
	if (fabs(ObjectiveValue) > MFA_ZERO_TOLERANCE && InParameters->CheckReactionEssentiality && !SubProblem) {
		//Inputing the current solution
		InputSolution(NewSolution);
		
		//Reaction essentiality data will be stored in these strings
		string OptimalEssentialReactions;
		string EssentialReactions;
		for (int i=0; i < InData->FNumReactions(); i++) {
			//Checking to see if the reaction is carrying flux in the current solution
			if (InData->GetReaction(i)->FFlux(NULL) > MFA_ZERO_TOLERANCE) {
				//Saving the current bounds
				double CurrentLowerBound = InData->GetReaction(i)->FluxLowerBound();
				double CurrentUpperBound = InData->GetReaction(i)->FluxUpperBound();
				//Reseting the bounds to zero
				InData->GetReaction(i)->ResetFluxBounds(0,0,this);
				//Running the MFA
				OptSolutionData* TempSolution = RunSolver(false,false,false);
				if (TempSolution != NULL && TempSolution->Status == SUCCESS) {
					if (TempSolution->Objective > MFA_ZERO_TOLERANCE && (TempSolution->Objective + MFA_ZERO_TOLERANCE) < ObjectiveValue) {
						//This reaction reduces the objective, but does not eliminate the objective
						if (OptimalEssentialReactions.length() > 0) {
							OptimalEssentialReactions.append(",");
						}
						OptimalEssentialReactions.append(InData->GetReaction(i)->GetData("DATABASE",STRING));
						OptimalEssentialReactions.append("(");
						OptimalEssentialReactions.append(dtoa(TempSolution->Objective/ObjectiveValue));
						OptimalEssentialReactions.append(")");
					} else if (TempSolution->Objective < MFA_ZERO_TOLERANCE) {
						//This reaction is essential for the objective to be nonzero
						if (EssentialReactions.length() > 0) {
							EssentialReactions.append(",");
						}
						EssentialReactions.append(InData->GetReaction(i)->GetData("DATABASE",STRING));
					}
				}
				//Restoring the original bounds
				InData->GetReaction(i)->ResetFluxBounds(CurrentLowerBound,CurrentUpperBound,this);
				delete TempSolution;
			}
		}
		//Saving the essentiality data in the solution note
		Note.append("|ESSENTIAL REACTIONS:");
		Note.append(EssentialReactions);
		Note.append("|OPTESSENTIAL REACTIONS:");
		Note.append(OptimalEssentialReactions);
	}

	//Checking which additional studies should be performed
	bool OptimizeMedia = false;
	if (fabs(ObjectiveValue) < MFA_ZERO_TOLERANCE) {
		DoFindTightBounds = false;
		MinimizeForeignReactions = false;
		DoFluxCouplingAnalysis = false;
		DoMILPCoessentiality = false;
		DoMinimizeReactions = false;
		DoIntervalOptimization = false;
		DoGeneOptimization = false;
		if (InParameters->OptimizeMediaWhenZero && !SubProblem && InParameters->AllDrainUse) {
			OptimizeMedia = true;
		}
	}
	//Optimizing individual metabolite production if the objective was zero and the analysis was requested
	if (fabs(ObjectiveValue) < MFA_ZERO_TOLERANCE && InParameters->OptimizeMetabolitesWhenZero && !SubProblem) {
		string NewNote;
		CheckIndividualMetaboliteProduction(InData,InParameters,GetParameter("metabolites to optimize"),false,false,NewNote,true);
	}
	//Reading the parameter indicating the acceptable fraction of the optimal that we will allow in the further studies performed and making the optimized single objective a constraint now
	if (DoMinimizeDeltaGError) {
		PrintProblemReport(NewSolution->Objective,InParameters,Note);
	}
	if (InParameters->DetermineMinimalMedia || DoMinimizeFlux || DoMinimizeDeltaGError || DoMinimizeReactions || DoFindTightBounds || MinimizeForeignReactions || OptimizeMedia || DoFluxCouplingAnalysis || DoMILPCoessentiality || DoIntervalOptimization || DoGeneOptimization) {
		//Creating the objective constriant
		LinEquation* ObjConst = NULL;
		bool OriginalTightBoundUse = UseTightBounds;
		ObjConst = MakeObjectiveConstraint(0,EQUAL);
		if (InParameters->DetermineMinimalMedia || DoMinimizeFlux || DoMinimizeDeltaGError || DoMinimizeReactions || DoFindTightBounds || MinimizeForeignReactions || DoFluxCouplingAnalysis || DoMILPCoessentiality || DoGeneOptimization || DoIntervalOptimization) {
			ObjConst->RightHandSide = InParameters->OptimalObjectiveFraction*ObjectiveValue;
			if (ObjectiveValue > 0) {
				ObjConst->EqualityType = GREATER;
			} else {		
				ObjConst->EqualityType = LESS;
			}
		}
		LoadConstToSolver(ObjConst->Index);
		if (InParameters->DetermineMinimalMedia) {
			this->DetermineMinimalFeasibleMedia(InData,InParameters,NewSolution);
		}
		//Minimizing error in the delta G values
		LinEquation* UseVarObjConst = NULL;
		LinEquation* EnergyErrorObjConst = NULL;
		if (DoMinimizeDeltaGError) {
			//Add the compound energy error use variables to the objective
			LinEquation* OldObjective = GetObjective();
			ObjFunct = InitializeLinEquation();
			SetMin();
			if (InParameters->ReactionErrorUseVariables) {
				for (int i=0; i < FNumVariables(); i++) {
					if (GetVariable(i)->Type == SMALL_DELTAG_ERROR_USE) {
						ObjFunct->Variables.push_back(GetVariable(i));
						ObjFunct->Coefficient.push_back(1);
					} else if (GetVariable(i)->Type == LARGE_DELTAG_ERROR_USE) {
						//ObjFunct->Variables.push_back(GetVariable(i));
						//ObjFunct->Coefficient.push_back(10);
					}
				}
				int Status = LoadObjective();
				if (Status != SUCCESS) {
					Note.append("Failed to load error minimization objective.");
					PrintProblemReport(FLAG,InParameters,Note);
					return FAIL;
				}
				//Running the solver and obtaining and checking the solution returned.
				NewSolution = RunSolver(true,true,true);
				double ObjectiveValue = NewSolution->Objective;
				if (NewSolution != NULL && NewSolution->Status == SUCCESS) {
					EnergyErrorObjConst = MakeObjectiveConstraint(ObjectiveValue,EQUAL);
					ObjFunct = OldObjective;
				}
			}
			
			//Add the compound energy error variables to the objective
			for (int i=0; i < FNumVariables(); i++) {
				if (GetVariable(i)->Type == DELTAGF_PERROR || GetVariable(i)->Type == DELTAGF_NERROR) {
					ObjFunct->Variables.push_back(GetVariable(i));
					ObjFunct->Coefficient.push_back(1);
				}
			}
			//Loading and running solver
			int Status = LoadObjective();
			if (Status != SUCCESS) {
				Note.append("Failed to load error minimization objective.");
				PrintProblemReport(FLAG,InParameters,Note);
				return FAIL;
			}
			//Running the solver and obtaining and checking the solution returned.
			NewSolution = RunSolver(true,true,true);
			double ObjectiveValue = NewSolution->Objective;
			if (NewSolution != NULL && NewSolution->Status == SUCCESS && NewSolution->Objective >= 1) {
				UseVarObjConst = MakeObjectiveConstraint(ObjectiveValue,EQUAL);
			}
		}
		//Finding the tight bounds if that is called for
		if (DoFindTightBounds || DoFluxCouplingAnalysis || DoMILPCoessentiality) {
			if (DoFindTightBounds && !InParameters->LoadTightBounds && InParameters->KOSets.size() == 0) {
				FindTightBounds(InData,InParameters,GetParameter("Save and print TightBound solutions").compare("1") == 0,true);
			}
			if (DoFluxCouplingAnalysis) {
				FluxCouplingAnalysis(InData,InParameters,true,Note,true);
			}
			if (DoMILPCoessentiality) {
				MILPCoessentialityAnalysis(InData,InParameters,false,Note,true);
			}
		}
		//Minimizing the intervals in the model
		if (DoIntervalOptimization) {
			vector<int> VariableTypes(1,INTERVAL_USE);
			RecursiveMILP(InData,InParameters,VariableTypes,true);
		}
		//Minimizing the genes in the model
		if (DoGeneOptimization) {
			vector<int> VariableTypes(1,GENE_USE);
			if (InParameters->DeletionOptimization) {
				VariableTypes.push_back(INTERVAL_USE);
				int MaxDeletions = atoi(GetParameter("Maximum number of deletions").data());
				int MinDeletions = atoi(GetParameter("Minimum number of deletions").data());
				//Finding the constraint on the total number of deletions allowed
				LinEquation* DeletionConstraint = NULL;
				for (int i=0; i < FNumConstraints(); i++) {
					if (GetConstraint(i)->ConstraintMeaning.compare("Constraint on the maximum number of deletions allowed") == 0) {
						DeletionConstraint = GetConstraint(i);
						break;
					}
				}
				if (DeletionConstraint != NULL) {
					for (int i=MinDeletions; i <= MaxDeletions; i++) {
						DeletionConstraint->RightHandSide = 2*i;
						LoadConstToSolver(DeletionConstraint->Index);
						RecursiveMILP(InData,InParameters,VariableTypes,true);
					}
				}
			} else {
				RecursiveMILP(InData,InParameters,VariableTypes,true);
			}
		}
		//Unrelaxing the integer variables if they have been relaxed
		if ((MinimizeForeignReactions || OptimizeMedia) && RelaxIntegerVariables) {
			RelaxIntegerVariables = false;
			ResetSolver();
		}
		//Minimizing the foreign reactions if that is called for
		if (MinimizeForeignReactions || DoMinimizeReactions) {
			int NumSolutions = 0;
			if (DoFindTightBounds) {
				UseTightBounds = true;
				ResetSolver();
			}

			vector<int> VarTypes;
			VarTypes.push_back(REACTION_USE);
			VarTypes.push_back(FORWARD_USE);
			VarTypes.push_back(REVERSE_USE);
			NumSolutions = RecursiveMILP(InData,InParameters,VarTypes,true);
		}
		if (DoMinimizeFlux) {
			LinEquation* CurrentObjective = ObjFunct;
			this->AddSumObjective(FLUX,false,false,1,false);
			this->AddSumObjective(REVERSE_FLUX,false,true,1,false);
			this->AddSumObjective(FORWARD_FLUX,false,true,1,false);
			this->SetMin();
			int Status = LoadObjective();
			if (Status != SUCCESS) {
				Note.append("Failed to load error flux minimization objective.");
				PrintProblemReport(FLAG,InParameters,Note);
				return FAIL;
			}

			//Running the solver to obtain a solution with minimal fluxes
			NewSolution = RunSolver(true,true,true);
			NewSolution->Objective = ObjectiveValue;
			ObjFunct = CurrentObjective;
		}
		if (OptimizeMedia) {
			//Saving the current objective function
			LinEquation* CurrentObjective = ObjFunct;
			bool OriginalSense = Max;

			//Altering the objective constraint to force the objective to be on
			ObjConst->RightHandSide = 0.001;
			ObjConst->EqualityType = GREATER;

			//Creating the new objective function
			ObjFunct = InitializeLinEquation("Minimal media objective");

			//Saving then altering bounds on all drain flux variables
			vector<double> OriginalDrainUpperBounds;
			vector<double> OriginalDrainLowerBounds;
			vector<MFAVariable*> MediaFluxes;
			for (int i=0; i < FNumVariables(); i++) {
				if ((GetVariable(i)->Type == FORWARD_DRAIN_FLUX || GetVariable(i)->Type == DRAIN_FLUX) && GetVariable(i)->LowerBound >= 0) {
					//Marking the current media compounds so they will not be added to the objective
					if (GetVariable(i)->UpperBound > 0 && GetVariable(i)->AssociatedSpecies != NULL) {
						GetVariable(i)->AssociatedSpecies->AddData("CURRENT MEDIA","YES",STRING);
					} else {
						OriginalDrainUpperBounds.push_back(GetVariable(i)->UpperBound);
						OriginalDrainLowerBounds.push_back(GetVariable(i)->LowerBound);
						GetVariable(i)->UpperBound = InParameters->MaxFlux;
						GetVariable(i)->LowerBound = 0;
						MediaFluxes.push_back(GetVariable(i));
					}
				}
			}

			//Adding compounds not currently in the media to the objective
			for (int i=0; i < FNumVariables(); i++) {
				if (GetVariable(i)->Type == FORWARD_DRAIN_USE || GetVariable(i)->Type == DRAIN_USE) {
					if (GetVariable(i)->AssociatedSpecies != NULL && GetVariable(i)->AssociatedSpecies->GetData("CURRENT MEDIA",STRING).compare("YES") != 0) {
						ObjFunct->Variables.push_back(GetVariable(i));
						ObjFunct->Coefficient.push_back(1);
					}
				}
			}
			SetMin();
			
			//Loading the problem
			ResetSolver();
			if (LoadSolver() == FAIL) {
				return FAIL;	
			}
		
			//Saving the number of solutions before running the recursive MILP
			int FirstMediaSolution = FNumSolutions();
			
			//Running the recursive MILP
			vector<int> VariableTypes;
			VariableTypes.push_back(OBJECTIVE_TERMS);
			int NumSolutions = RecursiveMILP(InData,InParameters,VariableTypes,false);

			//Determining which media components are involved in the solutions	
			string RestoringMedia;
			if (NumSolutions > 0) {
				Note.append("Minimal media addtions:");
				for (int i=FirstMediaSolution; i < FNumSolutions(); i++) {
					if (RestoringMedia.length() > 0) {
						RestoringMedia.append("|");
					}
					bool First = true;
					for (int j=0; j < int(ObjFunct->Variables.size()); j++) {
						if (GetSolution(i)->SolutionData[ObjFunct->Variables[j]->Index] > 0.5 && ObjFunct->Variables[j]->AssociatedSpecies != NULL) {
							if (!First) {
								RestoringMedia.append(",");
							}
							RestoringMedia.append(ObjFunct->Variables[j]->AssociatedSpecies->GetData("DATABASE",STRING));
							First = false;
						}
					}
					
				}
				Note.append(RestoringMedia);
			} else {
				RestoringMedia.assign("No feasible media formulations");
			}
			SetParameter("Restoring media",RestoringMedia.data());

			//Restoring the objective and drain flux bounds
			ObjFunct = CurrentObjective;
			Max = OriginalSense;
			for (int i=0; i < int(MediaFluxes.size()); i++) {
				MediaFluxes[i]->LowerBound = OriginalDrainLowerBounds[i];
				MediaFluxes[i]->UpperBound = OriginalDrainUpperBounds[i];
			}
		}
		//Removing the objective constraint and restoring integer variable relaxaction
		if (UseVarObjConst != NULL) {
			RemoveConstraint(UseVarObjConst->Index);
		}
		if (EnergyErrorObjConst != NULL) {
			RemoveConstraint(EnergyErrorObjConst->Index);
		}
		RemoveConstraint(ObjConst->Index);
		RelaxIntegerVariables = OriginalRelaxation;
		UseTightBounds = OriginalTightBoundUse;
		ResetSolver();
	}
	//Printing gene classes
	if (GetParameter("classify model genes").compare("1") == 0) {
		ofstream output;
		if (OpenOutput(output,FOutputFilepath()+"GeneClasses.txt")) {
			output << "Gene ID\tClassification" << endl;
			for (int i=0; i < InData->FNumGenes(); i++) {
				if (InData->GetGene(i)->GetData("CLASS",STRING).length() == 0) {
					InData->GetGene(i)->SetData("CLASS","Nonfunctional",STRING);
					for (int j=0; j < InData->GetGene(i)->FNumReactions(); j++) {
						MFAVariable* curVar = InData->GetGene(i)->GetReaction(j)->GetMFAVar(FORWARD_FLUX);
						if (curVar != NULL && (curVar->Max > MFA_ZERO_TOLERANCE || curVar->Min < -MFA_ZERO_TOLERANCE)) {
							InData->GetGene(i)->SetData("CLASS","Functional",STRING);
							break;
						}
						curVar = InData->GetGene(i)->GetReaction(j)->GetMFAVar(REVERSE_FLUX);
						if (curVar != NULL && (curVar->Max > MFA_ZERO_TOLERANCE || curVar->Min < -MFA_ZERO_TOLERANCE)) {
							InData->GetGene(i)->SetData("CLASS","Functional",STRING);
							break;
						}
						curVar = InData->GetGene(i)->GetReaction(j)->GetMFAVar(FLUX);
						if (curVar != NULL && (curVar->Max > MFA_ZERO_TOLERANCE || curVar->Min < -MFA_ZERO_TOLERANCE)) {
							InData->GetGene(i)->SetData("CLASS","Functional",STRING);
							break;
						}
					}
				}
				output << InData->GetGene(i)->GetData("DATABASE",STRING) << "\t" << InData->GetGene(i)->GetData("CLASS",STRING) << endl;
			}
			output.close();
		}
	}
	//Restoring integer variable relaxation if necessary
	if (RelaxIntegerVariables != OriginalRelaxation) {
		RelaxIntegerVariables = OriginalRelaxation;
		ResetSolver();
	}
	//Printing solution data
	if (!SubProblem) {
		PrintProblemReport(NewSolution->Objective,InParameters,Note);
		if (InParameters->ClearSolutions) {
			ClearSolutions(true);
		}
	} else if (InParameters->PrintSolutions) {
		PrintSolutions(-1,-1);
	}
	return SUCCESS;
}

int MFAProblem::CheckPotentialConstraints(Data* InData, OptimizationParameter* InParameters, double &ObjectiveValue, string Note){
	// created on 19 Nov 09 by keng.
	// This function adds the chemical potential energy constraint one-by-one and identifies which ones are prohibiting or reducing growth

	int Status = SUCCESS;
	ObjectiveValue = 0;
	vector<double> ObjValues;

	//Building the problem from the model if it has not already been built
	if (FNumVariables() == 0) {
		if (BuildMFAProblem(InData,InParameters) != SUCCESS) {
			FErrorFile() << "Failed to build optimization problem." << endl;
			FlushErrorFile();
			Note.append("Failed to build optimization problem");
			PrintProblemReport(FLAG,InParameters,Note);
			return FAIL;	
		}
	}
	
	//Loading the problem data into the solver if it is not already loaded... this automatically resets the solver
	if (ProblemLoaded) {
		Status = LoadObjective();
	} else {
		Status = LoadSolver(true);
	}
	if (Status != SUCCESS) {
		Note.append("Problem failed to load into solver");
		PrintProblemReport(FLAG,InParameters,Note);
		return FAIL;
	}

	//Printing the LP file rather than solving
	if (GetParameter("just print LP file").compare("1") == 0) {
		PrintVariableKey();
		WriteLPFile();
		return SUCCESS;
	}
	//Checking to ensure that the problem has an objective
	OptSolutionData* NewSolution = NULL;
	if (GetObjective() == NULL) {
		Note.append("Problem does not have objective");
		PrintProblemReport(FLAG,InParameters,Note);
		return FAIL;
	}
	//Running the solver and obtaining and checking the solution without potential constraints to make sure it is growing in the first place
	NewSolution = RunSolver(true,true,true);
	if (NewSolution == NULL) {
		Note.append("Problem failed to return a solution");
		PrintProblemReport(FLAG,InParameters,Note);
		return FAIL;
	} else if (NewSolution->Status != SUCCESS) {
		Note.append("Returned solution is infeasible");
		PrintProblemReport(FLAG,InParameters,Note);
		return FAIL;
	}
	
	ObjectiveValue = NewSolution->Objective;
	double OriginalObjValue = ObjectiveValue;

	FErrorFile() << "Original solution" << ObjectiveValue << endl;
	NewSolution->Notes.assign(Note);

	// if objective value is non-zero then we can continue to add in the potential constraints one at a time
	if (ObjectiveValue != 0) {
		int count = 1;

		for (int i=0; i < FNumConstraints(); i++) {
			// going through all the chemical potential constraints
			string ConstraintName = GetConstraintName(GetConstraint(i));

			FErrorFile() << ConstraintName << endl;
			FlushErrorFile();

			if (GetParameter("Check potential constraints feasibility").compare("1") == 0) {
				if ((GetConstraint(i)->ConstraintMeaning.compare("chemical potential constraint") == 0) && (GetConstraint(i)->RightHandSide < 0.9*FLAG)) {
					
					GetConstraint(i)->Loaded = true;

					if (LoadConstToSolver(i) != SUCCESS) {
						return FAIL;
					} else {
						WriteLPFile();
					}

					NewSolution = RunSolver(true,true,true);
					
					if (NewSolution == NULL) {
						Note.append("Problem failed to return a solution");
						PrintProblemReport(FLAG,InParameters,Note);
						return FAIL;
					} else if (NewSolution->Status != SUCCESS) {
						Note.append("Returned solution is infeasible");
						PrintProblemReport(FLAG,InParameters,Note);
						return FAIL;
					}
					
					ObjectiveValue = NewSolution->Objective;

					ObjValues.push_back(ObjectiveValue);
					
					cout << ConstraintName << "\t" << ObjectiveValue << endl;
					FErrorFile() << ConstraintName << "\t" << ObjectiveValue << endl;
					
					int Coeff = 1;

					if (OriginalObjValue < 0) {
						Coeff = -1;
					}

					double Cutoff = 0.001*Coeff*OriginalObjValue;

					// if the objective value becomes zero, we have to put back the relaxed constraint
					if (Coeff*ObjectiveValue < 0.5*Cutoff){
						cout << ConstraintName << " has to be relaxed." << endl;
						GetConstraint(i)->Loaded = false;
						if (LoadConstToSolver(i) != SUCCESS) {
							return FAIL;
						} else {
							WriteLPFile();
						}
					}
				}

				FlushErrorFile();
			}
		}
		return SUCCESS;
	} else {
		cout << "Objective value of problem without potential constraints is zero." << endl;
		return FAIL;
	}
}

int MFAProblem::OptimizeIndividualForeignReactions(Data* InData, OptimizationParameter* InParameters, bool FindTightBounds, bool OptimizeMetaboliteProduction) {
	//First I collect all of the foreign reactions into a vector
	vector<Reaction*> ForeignReactions;
	for (int i=0; i < InData->FNumReactions(); i++) {
		if (InData->GetReaction(i)->GetData("FOREIGN",STRING).length() > 0) {
			ForeignReactions.push_back(InData->GetReaction(i));
		}
	}

	if (ForeignReactions.size() == 0) {
		return FAIL;
	}

	//Next I set the bounds on each foreign reaction to zero
	for (int i=0; i < int(ForeignReactions.size()); i++) {
		ForeignReactions[i]->ResetFluxBounds(0,0,this);
	}

	//I replace the current objective with my formatted objective
	LinEquation* NewObjective = InitializeLinEquation("Foreign reaction objective",0,0,0);
	NewObjective->Coefficient.push_back(1);
	if (ForeignReactions[0]->GetMFAVar(FLUX) != NULL) {
		NewObjective->Variables.push_back(ForeignReactions[0]->GetMFAVar(FLUX));
	} else if (ForeignReactions[0]->GetMFAVar(FORWARD_FLUX) != NULL) {
		NewObjective->Variables.push_back(ForeignReactions[0]->GetMFAVar(FORWARD_FLUX));
		if (ForeignReactions[0]->GetMFAVar(REVERSE_FLUX) != NULL) {
			NewObjective->Coefficient.push_back(-1);
			NewObjective->Variables.push_back(ForeignReactions[0]->GetMFAVar(REVERSE_FLUX));
		}
	}
	AddObjective(NewObjective);
	SetMax();
	LoadSolver();

	//Now I individually activate each foreign reaction, optimize it, print the report, then turn it off again
	for (int i=0; i < int(ForeignReactions.size()); i++) {
		ForeignReactions[i]->ResetFluxBounds(0,100,this);
		NewObjective->Coefficient.clear();
		NewObjective->Variables.clear();
		NewObjective->Coefficient.push_back(1);
		if (ForeignReactions[i]->GetMFAVar(FLUX) != NULL) {
			NewObjective->Variables.push_back(ForeignReactions[i]->GetMFAVar(FLUX));
		} else if (ForeignReactions[i]->GetMFAVar(FORWARD_FLUX) != NULL) {
			NewObjective->Variables.push_back(ForeignReactions[i]->GetMFAVar(FORWARD_FLUX));
			if (ForeignReactions[i]->GetMFAVar(REVERSE_FLUX) != NULL) {
				NewObjective->Coefficient.push_back(-1);
				NewObjective->Variables.push_back(ForeignReactions[i]->GetMFAVar(REVERSE_FLUX));
			}
		}
		LoadObjective();
		double CurrentObjective = 0;
		string Note("Optimizing individual foreign reaction");
		OptimizeSingleObjective(InData,InParameters,FindTightBounds,false,CurrentObjective,Note,false);
		ForeignReactions[i]->ResetFluxBounds(0,0,this);
	}

	return SUCCESS;
}

int MFAProblem::FindSpecificExtremePathways(Data* InData, OptimizationParameter* InParameters) {
	int Status = SUCCESS;
	//First I build the problem from the model
	InParameters->AllDrainUse = true;
	InParameters->AllReactionsUse = true;
	InParameters->DrainUseVar = true;
	InParameters->ReactionsUse = true;
	RelaxIntegerVariables = false;
	if (BuildMFAProblem(InData,InParameters) != SUCCESS) {
		FErrorFile() << "Failed to build optimization problem." << endl;
		FlushErrorFile();
		return FAIL;	
	}
	
	//Now I load the problem into the solver
	LoadSolver();
	
	//I read in the list of reactions to find the extreme pathways for
	ifstream Input;
	string Filename = GetDatabaseDirectory(true)+GetParameter("Extreme pathway reaction list");
	if (!OpenInput(Input,Filename)) {
		return FAIL;
	}

	//Setting the variable types for the recursive MILP
	vector<int> VariableTypes;
	VariableTypes.push_back(REACTION_USE);
	VariableTypes.push_back(FORWARD_FLUX);
	VariableTypes.push_back(REVERSE_FLUX);
	VariableTypes.push_back(FORWARD_DRAIN_USE);
	VariableTypes.push_back(DRAIN_USE);

	//Finding the biomass species
	Species* Biomass = InData->FindSpecies("DATABASE;NAME","biomass");
	if (Biomass == NULL) {
		return FAIL;
	}

	//Setting the output filename and opening the output file
	ofstream Output;
	Filename = FOutputFilepath();
	Filename.append("ExtremePathwayStudyResults.txt");
	if (!OpenOutput(Output,Filename)) {
		return FAIL;
	}

	//Printing the file header
	Output << "REACTION;DIRECTION;COMPOUND UPTAKE;ACTIVE REACTIONS" << endl;

	double ForcingFlux = 0.00001;
	do {
		vector<string>* Strings = GetStringsFileline(Input,"\t");
		if (Strings->size() > 0) {
			for (int i=0; i < int(Strings->size()); i++) {
				Reaction* CurrentReaction = InData->FindReaction("DATABASE;NAME",(*Strings)[i].data());
				if (CurrentReaction != NULL) {
					MFAVariable* CurrentVariable = CurrentReaction->GetMFAVar(FORWARD_FLUX);
					if (CurrentVariable != NULL) {
						CurrentVariable = CurrentReaction->GetMFAVar(FLUX);
					}
					if (CurrentVariable != NULL) {
						//Forcing the current reaction to be "on"
						double OriginalLowerBound = CurrentVariable->LowerBound;
						CurrentVariable->LowerBound = ForcingFlux;
						LoadVariable(CurrentVariable->Index);
						ClearSolutions();
						//Running the recursive MILP to get all valid solutions
						int NumSolutions = RecursiveMILP(InData,InParameters,VariableTypes,false);
						for (int i=0; i < FNumSolutions(); i++) {
							InputSolution(GetSolution(i));
							//Printing the media and reactions participating in the current solution
							Output << CurrentReaction->GetData("DATABASE",STRING) << ";FORWARD;";
							for (int j=0; j < FNumVariables(); j++) {
								if ((GetVariable(j)->Type == DRAIN_FLUX || GetVariable(j)->Type == FORWARD_DRAIN_FLUX) && GetVariable(j)->Value > MFA_ZERO_TOLERANCE && GetVariable(j)->AssociatedSpecies != NULL) {
									Output << GetVariable(j)->AssociatedSpecies->GetData("DATABASE",STRING) << "|";
								}
							}
							Output << ";";
							for (int j=0; j < FNumVariables(); j++) {
								if ((GetVariable(j)->Type == FORWARD_FLUX || GetVariable(j)->Type == REVERSE_FLUX || GetVariable(j)->Type == FLUX) && GetVariable(j)->Value > MFA_ZERO_TOLERANCE && GetVariable(j)->AssociatedReaction != NULL) {
									Output << GetVariable(j)->AssociatedReaction->GetData("DATABASE",STRING) << "|";
								}
							}
						}
						//Restoring the original bounds to the variable
						CurrentVariable->LowerBound = OriginalLowerBound;
						LoadVariable(CurrentVariable->Index);
					}
					//Now attempting to force the reaction to run in reverse
					CurrentVariable = CurrentReaction->GetMFAVar(REVERSE_FLUX);
					if (CurrentVariable != NULL) {
						//Forcing the current reaction to be "on"
						double OriginalLowerBound = CurrentVariable->LowerBound;
						CurrentVariable->LowerBound = ForcingFlux;
						LoadVariable(CurrentVariable->Index);
						ClearSolutions();
						//Running the recursive MILP to get all valid solutions
						int NumSolutions = RecursiveMILP(InData,InParameters,VariableTypes,false);
						for (int i=0; i < FNumSolutions(); i++) {
							InputSolution(GetSolution(i));
							//Printing the media and reactions participating in the current solution
							Output << CurrentReaction->GetData("DATABASE",STRING) << ";REVERSE;";
							for (int j=0; j < FNumVariables(); j++) {
								if ((GetVariable(j)->Type == DRAIN_FLUX || GetVariable(j)->Type == FORWARD_DRAIN_FLUX) && GetVariable(j)->Value > MFA_ZERO_TOLERANCE && GetVariable(j)->AssociatedSpecies != NULL) {
									Output << GetVariable(j)->AssociatedSpecies->GetData("DATABASE",STRING) << "|";
								}
							}
							Output << ";";
							for (int j=0; j < FNumVariables(); j++) {
								if ((GetVariable(j)->Type == FORWARD_FLUX || GetVariable(j)->Type == REVERSE_FLUX || GetVariable(j)->Type == FLUX) && GetVariable(j)->Value > MFA_ZERO_TOLERANCE && GetVariable(j)->AssociatedReaction != NULL) {
									Output << GetVariable(j)->AssociatedReaction->GetData("DATABASE",STRING) << "|";
								}
							}
							Output << endl;
						}
						//Restoring the original bounds to the variable
						CurrentVariable->LowerBound = OriginalLowerBound;
						LoadVariable(CurrentVariable->Index);
					}
				}
			}	
		}
		delete Strings;
	} while(!Input.eof());
	Input.close();

	//Closing the output file
	Output.close();

	return SUCCESS;
}

int MFAProblem::FluxCouplingAnalysis(Data* InData, OptimizationParameter* InParameters, bool DoFindTightBounds, string &InNote, bool SubProblem) {
	//Notes on problem progress will be stored in this string
	InNote.append("Performing flux coupling analysis(");
	
	//Building the problem from the model if it has not already been built
	if (FNumVariables() == 0) {
		if (BuildMFAProblem(InData,InParameters) != SUCCESS) {
			FErrorFile() << "Failed to build optimization problem." << endl;
			FlushErrorFile();
			InNote.append("Failed to build optimization problem");
			if (!SubProblem) {
				PrintProblemReport(FLAG,InParameters,InNote);
			}
			return FAIL;	
		}
	}

	//Initially maximizing and minimizing each reaction to classify the reactions
	if (DoFindTightBounds) {
		FindTightBounds(InData,InParameters,false,true);
	}
	ResetIndecies();

	//Identifying and marking the variable flux variables
	ResetVariableMarks(false);
	vector<double> OriginalMin;
	vector<double> OriginalMax;
	vector<MFAVariable*> VariableFluxes;
	vector<bool> VariableDone;
	for (int i=0; i < FNumVariables(); i++) {
		if (GetVariable(i)->Min <= MFA_ZERO_TOLERANCE && GetVariable(i)->Max >= -MFA_ZERO_TOLERANCE && (GetVariable(i)->Max - GetVariable(i)->Min) > 10*MFA_ZERO_TOLERANCE && (GetVariable(i)->Type == FORWARD_FLUX || GetVariable(i)->Type == REVERSE_FLUX || GetVariable(i)->Type == FLUX)) {
			OriginalMin.push_back(GetVariable(i)->Min);
			OriginalMax.push_back(GetVariable(i)->Max);
			VariableFluxes.push_back(GetVariable(i));
			VariableDone.push_back(false);
			GetVariable(i)->Mark = true;
		}
	}

	//Knocking out each nonessential reaction and finding the tight bounds again
	map<string, map<string, vector<string>, std::less<string> >, std::less<string> > CoupledReactionMaps;
	map<string, map<string, vector<string>, std::less<string> >, std::less<string> > CoessentialReactionMaps;
	for (int i=0; i < int(VariableFluxes.size()); i++) {
		UseTightBounds = false;
		ResetSolver();
		LoadSolver();
		if (VariableFluxes[i]->AssociatedReaction != NULL) {
			//Saving the orginal bounds for this flux
			double LowerBound = VariableFluxes[i]->LowerBound;
			double UpperBound = VariableFluxes[i]->UpperBound;
			//Knocking out reaction by setting bounds to zero
			VariableFluxes[i]->LowerBound = 0;
			VariableFluxes[i]->UpperBound = 0;
			LoadVariable(VariableFluxes[i]->Index);
			//Unmarking the variable since I don't need to calculate its tight bounds
			VariableFluxes[i]->Mark = false;
			//Checking for problem feasibility
			OptSolutionData* TempSolution = RunSolver(false,false,true);
			if (TempSolution != NULL && TempSolution->Status == SUCCESS) {
				delete TempSolution;
				//Finding new tight bounds
				FindTightBounds(InData,InParameters,false);
				string Sign("?");
				if (LowerBound == 0) {
					Sign.assign("+");
				} else if (UpperBound == 0) {
					Sign.assign("-");
				}
				for (int j=0; j < int(VariableFluxes.size()); j++) {
					if (VariableFluxes[j]->Mark && VariableFluxes[j]->Min != FLAG && VariableFluxes[j]->Max != FLAG) {
						if (VariableFluxes[j]->Min > MFA_ZERO_TOLERANCE && VariableFluxes[j]->AssociatedReaction != NULL) {
							if (VariableFluxes[i]->AssociatedReaction->GetData("DATABASE",STRING) < VariableFluxes[j]->AssociatedReaction->GetData("DATABASE",STRING)) {
								vector<string>& Temp = CoessentialReactionMaps[VariableFluxes[i]->AssociatedReaction->GetData("DATABASE",STRING)][VariableFluxes[j]->AssociatedReaction->GetData("DATABASE",STRING)];
								if (Temp.size() == 0) {
									Temp.resize(4);
									Temp[0].assign(Sign);
									Temp[1].assign("+");
									Temp[2].assign("KO");
									Temp[3].assign(">");
								} else {
									Temp[0].append("/");
									Temp[0].append(Sign);
									Temp[1].append("/+");
									Temp[2].append("/KO");
									Temp[3].append("/>");
								}
							} else {
								vector<string>& Temp = CoessentialReactionMaps[VariableFluxes[j]->AssociatedReaction->GetData("DATABASE",STRING)][VariableFluxes[i]->AssociatedReaction->GetData("DATABASE",STRING)];
								if (Temp.size() == 0) {
									Temp.resize(4);
									Temp[0].assign("+");
									Temp[1].assign(Sign);
									Temp[2].assign("KO");
									Temp[3].assign("<");
								} else {
									Temp[0].append("/+");
									Temp[1].append("/");
									Temp[1].append(Sign);
									Temp[2].append("/KO");
									Temp[3].append("/<");
								}
							}
						} else if (VariableFluxes[j]->Max < -MFA_ZERO_TOLERANCE && VariableFluxes[j]->AssociatedReaction != NULL) {
							if (VariableFluxes[i]->AssociatedReaction->GetData("DATABASE",STRING) < VariableFluxes[j]->AssociatedReaction->GetData("DATABASE",STRING)) {
								vector<string>& Temp = CoessentialReactionMaps[VariableFluxes[i]->AssociatedReaction->GetData("DATABASE",STRING)][VariableFluxes[j]->AssociatedReaction->GetData("DATABASE",STRING)];
								if (Temp.size() == 0) {
									Temp.resize(4);
									Temp[0].assign(Sign);
									Temp[1].assign("-");
									Temp[2].assign("KO");
									Temp[3].assign(">");
								} else {
									Temp[0].append("/");
									Temp[0].append(Sign);
									Temp[1].append("/-");
									Temp[2].append("/KO");
									Temp[3].append("/>");
								}
							} else {
								vector<string>& Temp = CoessentialReactionMaps[VariableFluxes[j]->AssociatedReaction->GetData("DATABASE",STRING)][VariableFluxes[i]->AssociatedReaction->GetData("DATABASE",STRING)];
								if (Temp.size() == 0) {
									Temp.resize(4);
									Temp[0].assign("-");
									Temp[1].assign(Sign);
									Temp[2].assign("KO");
									Temp[3].assign("<");
								} else {
									Temp[0].append("-");
									Temp[1].append("/");
									Temp[1].append(Sign);
									Temp[2].append("KO");
									Temp[3].append("<");
								}
							}
						} else if (VariableFluxes[j]->Max < MFA_ZERO_TOLERANCE && VariableFluxes[j]->Min > -MFA_ZERO_TOLERANCE && VariableFluxes[j]->AssociatedReaction != NULL) {
							if (VariableFluxes[i]->AssociatedReaction->GetData("DATABASE",STRING) < VariableFluxes[j]->AssociatedReaction->GetData("DATABASE",STRING)) {
								vector<string>& Temp = CoupledReactionMaps[VariableFluxes[i]->AssociatedReaction->GetData("DATABASE",STRING)][VariableFluxes[j]->AssociatedReaction->GetData("DATABASE",STRING)];
								if (Temp.size() == 0) {
									Temp.resize(4);
									Temp[0].assign(Sign);
									Temp[1].assign("?");
									Temp[2].assign("KO");
									Temp[3].assign(">");
								} else {
									Temp[0].append("/");
									Temp[0].append(Sign);
									Temp[1].append("/?");
									Temp[2].append("/KO");
									Temp[3].append("/>");
								}
							} else {
								vector<string>& Temp = CoupledReactionMaps[VariableFluxes[j]->AssociatedReaction->GetData("DATABASE",STRING)][VariableFluxes[i]->AssociatedReaction->GetData("DATABASE",STRING)];
								if (Temp.size() == 0) {
									Temp.resize(4);
									Temp[0].assign("?");
									Temp[1].assign(Sign);
									Temp[2].assign("KO");
									Temp[3].assign("<");
								} else {
									Temp[0].append("/?");
									Temp[1].append("/");
									Temp[1].append(Sign);
									Temp[2].append("/KO");
									Temp[3].append("/<");
								}
							}
						}
					}
				}
			} else {
				FErrorFile() << "Model infeasible for " << VariableFluxes[i]->AssociatedReaction->GetData("DATABASE",STRING) << " KO" << endl;
				FlushErrorFile();
			}

			if (OriginalMax[i] > MFA_ZERO_TOLERANCE) {
				//Fixing the variable at its maximum value
				VariableFluxes[i]->LowerBound = OriginalMax[i];
				VariableFluxes[i]->UpperBound = OriginalMax[i];
				LoadVariable(VariableFluxes[i]->Index);
				//Checking for problem feasibility
				TempSolution = RunSolver(false,false,true);
				if (TempSolution != NULL && TempSolution->Status == SUCCESS) {
					delete TempSolution;
					//Finding new tight bounds
					FindTightBounds(InData,InParameters,false);
					//Identifying the reactions that are now blocked or have a reduced maximum
					for (int j=0; j < int(VariableFluxes.size()); j++) {
						if (VariableFluxes[j]->Mark && VariableFluxes[j]->Min != FLAG && VariableFluxes[j]->Max != FLAG) {
							if (VariableFluxes[j]->Min > MFA_ZERO_TOLERANCE && VariableFluxes[j]->AssociatedReaction != NULL) {
								if (VariableFluxes[i]->AssociatedReaction->GetData("DATABASE",STRING) < VariableFluxes[j]->AssociatedReaction->GetData("DATABASE",STRING)) {
									vector<string>& Temp = CoupledReactionMaps[VariableFluxes[i]->AssociatedReaction->GetData("DATABASE",STRING)][VariableFluxes[j]->AssociatedReaction->GetData("DATABASE",STRING)];
									if (Temp.size() == 0) {
										Temp.resize(4);
										Temp[0].assign("+");
										Temp[1].assign("+");
										Temp[2].assign("MAX");
										Temp[3].assign(">");
									} else {
										Temp[0].append("/+");
										Temp[1].append("/+");
										Temp[2].append("/MAX");
										Temp[3].append("/>");
									}
								} else {
									vector<string>& Temp = CoupledReactionMaps[VariableFluxes[j]->AssociatedReaction->GetData("DATABASE",STRING)][VariableFluxes[i]->AssociatedReaction->GetData("DATABASE",STRING)];
									if (Temp.size() == 0) {
										Temp.resize(4);
										Temp[0].assign("+");
										Temp[1].assign("+");
										Temp[2].assign("MAX");
										Temp[3].assign("<");
									} else {
										Temp[0].append("/+");
										Temp[1].append("/+");
										Temp[2].append("/MAX");
										Temp[3].append("/<");
									}
								}
							} else if (VariableFluxes[j]->Max < -MFA_ZERO_TOLERANCE && VariableFluxes[j]->AssociatedReaction != NULL) {
								if (VariableFluxes[i]->AssociatedReaction->GetData("DATABASE",STRING) < VariableFluxes[j]->AssociatedReaction->GetData("DATABASE",STRING)) {
									vector<string>& Temp = CoupledReactionMaps[VariableFluxes[i]->AssociatedReaction->GetData("DATABASE",STRING)][VariableFluxes[j]->AssociatedReaction->GetData("DATABASE",STRING)];
									if (Temp.size() == 0) {
										Temp.resize(4);
										Temp[0].assign("+");
										Temp[1].assign("-");
										Temp[2].assign("MAX");
										Temp[3].assign(">");
									} else {
										Temp[0].append("/+");
										Temp[1].append("/-");
										Temp[2].append("/MAX");
										Temp[3].append("/>");
									}
								} else {
									vector<string>& Temp = CoupledReactionMaps[VariableFluxes[j]->AssociatedReaction->GetData("DATABASE",STRING)][VariableFluxes[i]->AssociatedReaction->GetData("DATABASE",STRING)];
									if (Temp.size() == 0) {
										Temp.resize(4);
										Temp[0].assign("-");
										Temp[1].assign("+");
										Temp[2].assign("MAX");
										Temp[3].assign("<");
									} else {
										Temp[0].append("/-");
										Temp[1].append("/+");
										Temp[2].append("/MAX");
										Temp[3].append("/<");
									}
								}						
							} else if (VariableFluxes[j]->Max < MFA_ZERO_TOLERANCE && VariableFluxes[j]->Min > -MFA_ZERO_TOLERANCE && VariableFluxes[j]->AssociatedReaction != NULL) {
								if (VariableFluxes[i]->AssociatedReaction->GetData("DATABASE",STRING) < VariableFluxes[j]->AssociatedReaction->GetData("DATABASE",STRING)) {
									vector<string>& Temp = CoessentialReactionMaps[VariableFluxes[i]->AssociatedReaction->GetData("DATABASE",STRING)][VariableFluxes[j]->AssociatedReaction->GetData("DATABASE",STRING)];
									if (Temp.size() == 0) {
										Temp.resize(4);
										Temp[0].assign("+");
										Temp[1].assign("?");
										Temp[2].assign("MAX(0)");
										Temp[3].assign(">");
									} else {
										Temp[0].append("/+");
										Temp[1].append("/?");
										Temp[2].append("/MAX(0)");
										Temp[3].append("/>");
									}
								} else {
									vector<string>& Temp = CoessentialReactionMaps[VariableFluxes[j]->AssociatedReaction->GetData("DATABASE",STRING)][VariableFluxes[i]->AssociatedReaction->GetData("DATABASE",STRING)];
									if (Temp.size() == 0) {
										Temp.resize(4);
										Temp[0].assign("?");
										Temp[1].assign("+");
										Temp[2].assign("MAX(0)");
										Temp[3].assign("<");
									} else {
										Temp[0].append("/?");
										Temp[1].append("/+");
										Temp[2].append("/MAX(0)");
										Temp[3].append("/<");
									}
								}
							} else if (VariableFluxes[j]->Max > MFA_ZERO_TOLERANCE && VariableFluxes[j]->Max/OriginalMax[j] < CRITICAL_FRACTION) {
								if (VariableFluxes[i]->AssociatedReaction->GetData("DATABASE",STRING) < VariableFluxes[j]->AssociatedReaction->GetData("DATABASE",STRING)) {
									vector<string>& Temp = CoessentialReactionMaps[VariableFluxes[i]->AssociatedReaction->GetData("DATABASE",STRING)][VariableFluxes[j]->AssociatedReaction->GetData("DATABASE",STRING)];
									if (Temp.size() == 0) {
										Temp.resize(4);
										Temp[0].assign("+");
										Temp[1].assign("+");
										Temp[2].assign("MAX(");
										Temp[2].append(dtoa(VariableFluxes[j]->Max/OriginalMax[j]));
										Temp[2].append(")");
										Temp[3].assign(">");
									} else {
										Temp[0].append("/+");
										Temp[1].append("/+");
										Temp[2].append("/MAX(");
										Temp[2].append(dtoa(VariableFluxes[j]->Max/OriginalMax[j]));
										Temp[2].append(")");
										Temp[3].append("/>");
									}
								} else {
									vector<string>& Temp = CoessentialReactionMaps[VariableFluxes[j]->AssociatedReaction->GetData("DATABASE",STRING)][VariableFluxes[i]->AssociatedReaction->GetData("DATABASE",STRING)];
									if (Temp.size() == 0) {
										Temp.resize(4);
										Temp[0].assign("+");
										Temp[1].assign("+");
										Temp[2].assign("MAX(");
										Temp[2].append(dtoa(VariableFluxes[j]->Max/OriginalMax[j]));
										Temp[2].append(")");
										Temp[3].assign("<");
									} else {
										Temp[0].append("/+");
										Temp[1].append("/+");
										Temp[2].append("/MAX(");
										Temp[2].append(dtoa(VariableFluxes[j]->Max/OriginalMax[j]));
										Temp[2].append(")");
										Temp[3].append("/<");
									}
								}
							} else if (VariableFluxes[j]->Min < -MFA_ZERO_TOLERANCE && VariableFluxes[j]->Min/OriginalMin[j] < CRITICAL_FRACTION) {
								if (VariableFluxes[i]->AssociatedReaction->GetData("DATABASE",STRING) < VariableFluxes[j]->AssociatedReaction->GetData("DATABASE",STRING)) {
									vector<string>& Temp = CoessentialReactionMaps[VariableFluxes[i]->AssociatedReaction->GetData("DATABASE",STRING)][VariableFluxes[j]->AssociatedReaction->GetData("DATABASE",STRING)];
									if (Temp.size() == 0) {
										Temp.resize(4);
										Temp[0].assign("+");
										Temp[1].assign("-");
										Temp[2].assign("MAX(");
										Temp[2].append(dtoa(VariableFluxes[j]->Min/OriginalMin[j]));
										Temp[2].append(")");
										Temp[3].assign(">");
									} else {
										Temp[0].append("/+");
										Temp[1].append("/-");
										Temp[2].append("/MAX(");
										Temp[2].append(dtoa(VariableFluxes[j]->Min/OriginalMin[j]));
										Temp[2].append(")");
										Temp[3].append("/>");
									}
								} else {
									vector<string>& Temp = CoessentialReactionMaps[VariableFluxes[j]->AssociatedReaction->GetData("DATABASE",STRING)][VariableFluxes[i]->AssociatedReaction->GetData("DATABASE",STRING)];
									if (Temp.size() == 0) {
										Temp.resize(4);
										Temp[0].assign("+");
										Temp[1].assign("-");
										Temp[2].assign("MAX(");
										Temp[2].append(dtoa(VariableFluxes[j]->Min/OriginalMin[j]));
										Temp[2].append(")");
										Temp[3].assign("<");
									} else {
										Temp[0].append("/+");
										Temp[1].append("/-");
										Temp[2].append("/MAX(");
										Temp[2].append(dtoa(VariableFluxes[j]->Min/OriginalMin[j]));
										Temp[2].append(")");
										Temp[3].append("/<");
									}
								}
							}
						}
					}
				} else {
					FErrorFile() << "Model infeasible for " << VariableFluxes[i]->AssociatedReaction->GetData("DATABASE",STRING) << " MAX " << OriginalMax[i] << endl;
					FlushErrorFile();
				}
			}

			if (OriginalMin[i] < -MFA_ZERO_TOLERANCE) {
				//Fixing the variable at its maximum value
				VariableFluxes[i]->LowerBound = OriginalMin[i];
				VariableFluxes[i]->UpperBound = OriginalMin[i];
				LoadVariable(VariableFluxes[i]->Index);
				//Checking for problem feasibility
				TempSolution = RunSolver(false,false,true);
				if (TempSolution != NULL && TempSolution->Status == SUCCESS) {
					delete TempSolution;
					//Finding new tight bounds
					FindTightBounds(InData,InParameters,false);
					//Identifying the reactions that are now blocked or have a reduced maximum
					for (int j=0; j < int(VariableFluxes.size()); j++) {
						if (VariableFluxes[j]->Mark && VariableFluxes[j]->Min != FLAG && VariableFluxes[j]->Max != FLAG) {
							if (VariableFluxes[j]->Min > MFA_ZERO_TOLERANCE && VariableFluxes[j]->AssociatedReaction != NULL) {
								if (VariableFluxes[i]->AssociatedReaction->GetData("DATABASE",STRING) < VariableFluxes[j]->AssociatedReaction->GetData("DATABASE",STRING)) {
									vector<string>& Temp = CoupledReactionMaps[VariableFluxes[i]->AssociatedReaction->GetData("DATABASE",STRING)][VariableFluxes[j]->AssociatedReaction->GetData("DATABASE",STRING)];
									if (Temp.size() == 0) {
										Temp.resize(4);
										Temp[0].assign("-");
										Temp[1].assign("+");
										Temp[2].assign("MIN");
										Temp[3].assign(">");
									} else {
										Temp[0].append("/-");
										Temp[1].append("/+");
										Temp[2].append("/MIN");
										Temp[3].append("/>");
									}
								} else {
									vector<string>& Temp = CoupledReactionMaps[VariableFluxes[j]->AssociatedReaction->GetData("DATABASE",STRING)][VariableFluxes[i]->AssociatedReaction->GetData("DATABASE",STRING)];
									if (Temp.size() == 0) {
										Temp.resize(4);
										Temp[0].assign("+");
										Temp[1].assign("-");
										Temp[2].assign("MIN");
										Temp[3].assign("<");
									} else {
										Temp[0].append("/+");
										Temp[1].append("/-");
										Temp[2].append("/MIN");
										Temp[3].append("/<");
									}
								}
							} else if (VariableFluxes[j]->Max < -MFA_ZERO_TOLERANCE && VariableFluxes[j]->AssociatedReaction != NULL) {
								if (VariableFluxes[i]->AssociatedReaction->GetData("DATABASE",STRING) < VariableFluxes[j]->AssociatedReaction->GetData("DATABASE",STRING)) {
									vector<string>& Temp = CoupledReactionMaps[VariableFluxes[i]->AssociatedReaction->GetData("DATABASE",STRING)][VariableFluxes[j]->AssociatedReaction->GetData("DATABASE",STRING)];
									if (Temp.size() == 0) {
										Temp.resize(4);
										Temp[0].assign("-");
										Temp[1].assign("-");
										Temp[2].assign("MIN");
										Temp[3].assign(">");
									} else {
										Temp[0].append("/-");
										Temp[1].append("/-");
										Temp[2].append("/MIN");
										Temp[3].append("/>");
									}
								} else {
									vector<string>& Temp = CoupledReactionMaps[VariableFluxes[j]->AssociatedReaction->GetData("DATABASE",STRING)][VariableFluxes[i]->AssociatedReaction->GetData("DATABASE",STRING)];
									if (Temp.size() == 0) {
										Temp.resize(4);
										Temp[0].assign("-");
										Temp[1].assign("-");
										Temp[2].assign("MIN");
										Temp[3].assign("<");
									} else {
										Temp[0].append("/-");
										Temp[1].append("/-");
										Temp[2].append("/MIN");
										Temp[3].append("/<");
									}
								}
							} else if (VariableFluxes[j]->Max < MFA_ZERO_TOLERANCE && VariableFluxes[j]->Min > -MFA_ZERO_TOLERANCE && VariableFluxes[j]->AssociatedReaction != NULL) {
								if (VariableFluxes[i]->AssociatedReaction->GetData("DATABASE",STRING) < VariableFluxes[j]->AssociatedReaction->GetData("DATABASE",STRING)) {
									vector<string>& Temp = CoessentialReactionMaps[VariableFluxes[i]->AssociatedReaction->GetData("DATABASE",STRING)][VariableFluxes[j]->AssociatedReaction->GetData("DATABASE",STRING)];
									if (Temp.size() == 0) {
										Temp.resize(4);
										Temp[0].assign("-");
										Temp[1].assign("?");
										Temp[2].assign("MIN(0)");
										Temp[3].assign(">");
									} else {
										Temp[0].append("/-");
										Temp[1].append("/?");
										Temp[2].append("/MIN(0)");
										Temp[3].append("/>");
									}
								} else {
									vector<string>& Temp = CoessentialReactionMaps[VariableFluxes[j]->AssociatedReaction->GetData("DATABASE",STRING)][VariableFluxes[i]->AssociatedReaction->GetData("DATABASE",STRING)];
									if (Temp.size() == 0) {
										Temp.resize(4);
										Temp[0].assign("?");
										Temp[1].assign("-");
										Temp[2].assign("MIN(0)");
										Temp[3].assign("<");
									} else {
										Temp[0].append("/?");
										Temp[1].append("/-");
										Temp[2].append("/MIN(0)");
										Temp[3].append("/<");
									}
								}
							} else if (VariableFluxes[j]->Max > MFA_ZERO_TOLERANCE && VariableFluxes[j]->Max/OriginalMax[j] < CRITICAL_FRACTION) {
								if (VariableFluxes[i]->AssociatedReaction->GetData("DATABASE",STRING) < VariableFluxes[j]->AssociatedReaction->GetData("DATABASE",STRING)) {
									vector<string>& Temp = CoessentialReactionMaps[VariableFluxes[i]->AssociatedReaction->GetData("DATABASE",STRING)][VariableFluxes[j]->AssociatedReaction->GetData("DATABASE",STRING)];
									if (Temp.size() == 0) {
										Temp.resize(4);
										Temp[0].assign("-");
										Temp[1].assign("+");
										Temp[2].assign("MIN(");
										Temp[2].append(dtoa(VariableFluxes[j]->Max/OriginalMax[j]));
										Temp[2].append(")");
										Temp[3].assign(">");
									} else {
										Temp[0].append("/-");
										Temp[1].append("/+");
										Temp[2].append("/MIN(");
										Temp[2].append(dtoa(VariableFluxes[j]->Max/OriginalMax[j]));
										Temp[2].append(")");
										Temp[3].append("/>");
									}
								} else {
									vector<string>& Temp = CoessentialReactionMaps[VariableFluxes[j]->AssociatedReaction->GetData("DATABASE",STRING)][VariableFluxes[i]->AssociatedReaction->GetData("DATABASE",STRING)];
									if (Temp.size() == 0) {
										Temp.resize(4);
										Temp[0].assign("+");
										Temp[1].assign("-");
										Temp[2].assign("MIN(");
										Temp[2].append(dtoa(VariableFluxes[j]->Max/OriginalMax[j]));
										Temp[2].append(")");
										Temp[3].assign("<");
									} else {
										Temp[0].append("/+");
										Temp[1].append("/-");
										Temp[2].append("/MIN(");
										Temp[2].append(dtoa(VariableFluxes[j]->Max/OriginalMax[j]));
										Temp[2].append(")");
										Temp[3].append("/<");
									}
								}
							} else if (VariableFluxes[j]->Min < -MFA_ZERO_TOLERANCE && VariableFluxes[j]->Min/OriginalMin[j] < CRITICAL_FRACTION) {
								if (VariableFluxes[i]->AssociatedReaction->GetData("DATABASE",STRING) < VariableFluxes[j]->AssociatedReaction->GetData("DATABASE",STRING)) {
									vector<string>& Temp = CoessentialReactionMaps[VariableFluxes[i]->AssociatedReaction->GetData("DATABASE",STRING)][VariableFluxes[j]->AssociatedReaction->GetData("DATABASE",STRING)];
									if (Temp.size() == 0) {
										Temp.resize(4);
										Temp[0].assign("-");
										Temp[1].assign("-");
										Temp[2].assign("MIN(");
										Temp[2].append(dtoa(VariableFluxes[j]->Min/OriginalMin[j]));
										Temp[2].append(")");
										Temp[3].assign(">");
									} else {
										Temp[0].append("/-");
										Temp[1].append("/-");
										Temp[2].append("/MIN(");
										Temp[2].append(dtoa(VariableFluxes[j]->Min/OriginalMin[j]));
										Temp[2].append(")");
										Temp[3].append("/>");
									}
								} else {
									vector<string>& Temp = CoessentialReactionMaps[VariableFluxes[j]->AssociatedReaction->GetData("DATABASE",STRING)][VariableFluxes[i]->AssociatedReaction->GetData("DATABASE",STRING)];
									if (Temp.size() == 0) {
										Temp.resize(4);
										Temp[0].assign("-");
										Temp[1].assign("-");
										Temp[2].assign("MIN(");
										Temp[2].append(dtoa(VariableFluxes[j]->Min/OriginalMin[j]));
										Temp[2].append(")");
										Temp[3].assign("<");
									} else {
										Temp[0].append("/-");
										Temp[1].append("/-");
										Temp[2].append("/MIN(");
										Temp[2].append(dtoa(VariableFluxes[j]->Min/OriginalMin[j]));
										Temp[2].append(")");
										Temp[3].append("/<");
									}
								}
							}
						}
					}
				} else {
					FErrorFile() << "Model infeasible for " << VariableFluxes[i]->AssociatedReaction->GetData("DATABASE",STRING) << " MIN " << OriginalMin[i] << endl;
					FlushErrorFile();
				}
			}

			//Restoring original variable bounds
			VariableFluxes[i]->LowerBound = LowerBound;
			VariableFluxes[i]->UpperBound = UpperBound;
			LoadVariable(VariableFluxes[i]->Index);
			//Remarking variable
			VariableFluxes[i]->Mark = true;
		}
	}
	
	//Saving the data into the Note string
	InNote.append("Coupled reactions(");
	for (map<string, map<string, vector<string>, std::less<string> >, std::less<string> >::iterator MapIT = CoupledReactionMaps.begin(); MapIT != CoupledReactionMaps.end(); MapIT++) {
		for (map<string, vector<string>, std::less<string> >::iterator MapITT = MapIT->second.begin(); MapITT != MapIT->second.end(); MapITT++) {
			InNote.append(MapIT->first);
			InNote.append(":");
			InNote.append(MapITT->first);
			for (int i=0; i < int(MapITT->second.size()); i++) {
				InNote.append(":");
				InNote.append(MapITT->second[i]);
			}
			InNote.append("|");
		}
		
	}

	InNote.append(") Coessential reactions(");
	for (map<string, map<string, vector<string>, std::less<string> >, std::less<string> >::iterator MapIT = CoessentialReactionMaps.begin(); MapIT != CoessentialReactionMaps.end(); MapIT++) {
		for (map<string, vector<string>, std::less<string> >::iterator MapITT = MapIT->second.begin(); MapITT != MapIT->second.end(); MapITT++) {
			InNote.append(MapIT->first);
			InNote.append(":");
			InNote.append(MapITT->first);
			for (int i=0; i < int(MapITT->second.size()); i++) {
				InNote.append(":");
				InNote.append(MapITT->second[i]);
			}
			InNote.append("|");
		}
	}

	InNote.append("))");

	if (!SubProblem) {
		PrintProblemReport(FLAG,InParameters,InNote);
	}

	return SUCCESS;
}

int MFAProblem::ExploreSplittingRatios(Data* InData, OptimizationParameter* InParameters, bool FindTightBounds, bool MinimizeForeignReactions) {
	bool OriginalPrint = InParameters->PrintSolutions;
	bool OriginalClear = InParameters->ClearSolutions;
	InParameters->PrintSolutions = false;
	InParameters->ClearSolutions = false;
	
	int Status = SUCCESS;
	string OriginalNote("Flux splitting ratio experiment:");
	
	//First I build the problem from the model if it has not already been built
	if (FNumVariables() == 0) {
		if (BuildMFAProblem(InData,InParameters) != SUCCESS) {
			FErrorFile() << "Failed to build optimization problem." << endl;
			FlushErrorFile();
			OriginalNote.append("Failed to build optimization problem");
			PrintProblemReport(FLAG,InParameters,OriginalNote);
			return FAIL;	
		}
	}
	
	if (ConvertStringToObjective(GetParameter("objective"),InData) == NULL) {
		FErrorFile() << "Failed to create objective." << endl;
		FlushErrorFile();
		OriginalNote.append("Failed to create objective.");
		PrintProblemReport(FLAG,InParameters,OriginalNote);
		return FAIL;
	}

	//Finding the variables associated with the specified ratios
	vector<vector<MFAVariable*> > ExplorationVariables;
	vector<vector<double> > ExplorationCoefficients;
	vector<vector<int> > ExplorationTypes;
	vector<double> MinValues;
	vector<double> MaxValues;
	vector<double> IterationValues;
	for (int j=0; j < int(InParameters->ExplorationNames.size()); j++) {
		bool Valid = true;
		vector<MFAVariable*> CurrentVariables;
		vector<double> CurrentCoefficients;
		vector<int> CurrentTypes;
		for (int k=0; k < int(InParameters->ExplorationNames[j].size()); k++) {
			//Searching for the species or reaction corresponding to the input name
			Species* TempSpecies = NULL;
			Reaction* TempReaction = NULL;
			if (InParameters->ExplorationTypes[j][k] == CONC || InParameters->ExplorationTypes[j][k] == LOG_CONC || InParameters->ExplorationTypes[j][k] == DRAIN_FLUX || InParameters->ExplorationTypes[j][k] == DELTAGF_ERROR) {
				TempSpecies = InData->FindSpecies("DATABASE;NAME",InParameters->ExplorationNames[j][k].data());
			} else {
				TempReaction = InData->FindReaction("DATABASE;NAME",InParameters->ExplorationNames[j][k].data());
			}
			//Searching for the variables corresponding to the input reaction/species and the input variable type
			if (TempSpecies == NULL && TempReaction == NULL) {
				Valid = false;
				break;
			}
			if (TempReaction != NULL) {
				MFAVariable* TempVariable = TempReaction->GetMFAVar(InParameters->ExplorationTypes[j][k]);
				if (TempVariable == NULL) {
					if (InParameters->ExplorationTypes[j][k] == FLUX) {
						Valid = false;
						TempVariable = TempReaction->GetMFAVar(FORWARD_FLUX);
						if (TempVariable != NULL) {
							Valid = true;
							CurrentVariables.push_back(TempVariable);
							CurrentCoefficients.push_back(InParameters->ExplorationCoefficients[j][k]);
							CurrentTypes.push_back(FORWARD_FLUX);
						}
						TempVariable = TempReaction->GetMFAVar(REVERSE_FLUX);
						if (TempVariable != NULL) {
							Valid = true;
							CurrentVariables.push_back(TempVariable);
							CurrentCoefficients.push_back(-InParameters->ExplorationCoefficients[j][k]);
							CurrentTypes.push_back(REVERSE_FLUX);
						}
						if (!Valid) {
							break;
						}
					} else {
						Valid = false;
						break;
					}
				} else {
					CurrentTypes.push_back(FLUX);
					CurrentVariables.push_back(TempVariable);
					CurrentCoefficients.push_back(InParameters->ExplorationCoefficients[j][k]);
				}
			} else if (TempSpecies != NULL) {
				MFAVariable* TempVariable = TempSpecies->GetMFAVar(InParameters->ExplorationTypes[j][k]);
				if (TempVariable == NULL) {
					if (InParameters->ExplorationTypes[j][k] == DRAIN_FLUX) {
						Valid = false;
						TempVariable = TempReaction->GetMFAVar(FORWARD_DRAIN_FLUX);
						if (TempVariable != NULL) {
							Valid = true;
							CurrentVariables.push_back(TempVariable);
							CurrentCoefficients.push_back(InParameters->ExplorationCoefficients[j][k]);
							CurrentTypes.push_back(FORWARD_DRAIN_FLUX);
						}
						TempVariable = TempReaction->GetMFAVar(REVERSE_DRAIN_FLUX);
						if (TempVariable != NULL) {
							Valid = true;
							CurrentVariables.push_back(TempVariable);
							CurrentCoefficients.push_back(-InParameters->ExplorationCoefficients[j][k]);
							CurrentTypes.push_back(REVERSE_DRAIN_FLUX);
						}
						if (!Valid) {
							break;
						}
					} else {
						Valid = false;
						break;
					}
				} else {
					CurrentTypes.push_back(DRAIN_FLUX);
					CurrentVariables.push_back(TempVariable);
					CurrentCoefficients.push_back(InParameters->ExplorationCoefficients[j][k]);
				}
			}
		}
		//Saving the exploration study data if all variabes are valid
		if (Valid) {
			ExplorationTypes.push_back(CurrentTypes);
			ExplorationVariables.push_back(CurrentVariables);
			ExplorationCoefficients.push_back(CurrentCoefficients);
			MinValues.push_back(InParameters->ExplorationMin[j]);
			MaxValues.push_back(InParameters->ExplorationMax[j]);
			IterationValues.push_back(InParameters->ExplorationIteration[j]);
		}
	}

	//Vectors to store solution data
	vector<vector<double> > Values(1);
	vector<double> Objectives;
	vector<LinEquation*> ExplorationConstraints;
	for (int j=0; j < int(ExplorationVariables.size()); j++) {
		Values[0].push_back(MinValues[j]);
	}

	//Counting through the ratio values
	do {
		//Copying the previous ratio values into the current ratio values
		vector<double> CurrentValues(ExplorationVariables.size());
		for (int i=0; i < int(ExplorationVariables.size()); i++) {
			CurrentValues[i] = Values[Values.size()-1][i];
		}
		
		//Iterating the last index by one
		int CurrentIndex = int(ExplorationVariables.size()-1);
		do {
			if (CurrentValues[CurrentIndex] < MaxValues[CurrentIndex]) {
				if (CurrentValues[CurrentIndex]+IterationValues[CurrentIndex] > MaxValues[CurrentIndex]) {
					CurrentValues[CurrentIndex] = MaxValues[CurrentIndex];
					CurrentIndex++;
				} else {
					CurrentValues[CurrentIndex] = CurrentValues[CurrentIndex]+IterationValues[CurrentIndex];
					CurrentIndex++;
				}
			} else {
				CurrentValues[CurrentIndex] = MinValues[CurrentIndex]-IterationValues[CurrentIndex];
				CurrentIndex--;
			}
		} while(CurrentIndex < int(ExplorationVariables.size()) && CurrentIndex != -1);
		Values.push_back(CurrentValues);
		if (CurrentIndex == -1) {
			break;
		}
	} while(1);
	Values.pop_back();

	//Now I load the solver once and only once to make the runs go faster
	if (!FProblemLoaded()) {
		LoadSolver();
	}

	//Creating exploration constraints
	for (int j=0; j < int(ExplorationVariables.size()); j++) {
		LinEquation* NewConstraint = InitializeLinEquation("Exploration constraint",0);
		for (int k=0; k < int(ExplorationVariables[j].size()); k++) {
			NewConstraint->Coefficient.push_back(ExplorationCoefficients[j][k]);
			NewConstraint->Variables.push_back(ExplorationVariables[j][k]);
		}
		NewConstraint->EqualityType = EQUAL;
		AddConstraint(NewConstraint);
		ExplorationConstraints.push_back(NewConstraint);
	}

	//Optimizing at every possible ratio condition
	for (int i=0; i < int(Values.size()); i++) {
		//Setting constraint values
		for (int j=0; j < int(Values[i].size()); j++) {
			ExplorationConstraints[j]->RightHandSide = Values[i][j];
			LoadConstToSolver(ExplorationConstraints[j]->Index);
		}
		double ObjectiveValue = 0;
		Status = OptimizeSingleObjective(InData,InParameters,FindTightBounds,MinimizeForeignReactions,ObjectiveValue,OriginalNote, true);
		Objectives.push_back(ObjectiveValue);
	}

	//Now I print all output
	ofstream Output;
	string Filename = FOutputFilepath();
	Filename.append("ExplorationStudyResults");
	Filename.append(itoa(ProblemIndex));
	Filename.append(".txt");
	if (!OpenOutput(Output,Filename)) {
		return FAIL;
	}

	//Printing the title line
	for (int j=0; j < int(ExplorationVariables.size()); j++) {
		for (int k=0; k < int(ExplorationVariables[j].size()); k++) {
			if (k > 0) {
				Output << " + ";
			}
			if (ExplorationCoefficients[j][k] != 1) {
				Output << "(" << ExplorationCoefficients[j][k] << ")";
			}
			if (ExplorationVariables[j][k]->AssociatedSpecies != NULL) {
				Output << ExplorationVariables[j][k]->AssociatedSpecies->GetData("DATABASE",STRING) << ":" << ConvertVariableType(ExplorationTypes[j][k]);
			} else if (ExplorationVariables[j][k]->AssociatedReaction != NULL) {
				Output << ExplorationVariables[j][k]->AssociatedReaction->GetData("DATABASE",STRING) << ":" << ConvertVariableType(ExplorationTypes[j][k]);
			}
		}
		Output << ";";
	}
	Output << "Objective" << endl;

	//Printing data
	for (int i=0; i < int(Values.size()); i++) {
		for(int j=0; j < int(Values[i].size()); j++) {
			Output << Values[i][j] << ";";
		}
		Output << Objectives[i] << endl;
	}
	Output.close();

	//Removing the ratio constraints
	for (int i=0; i < int(ExplorationConstraints.size()); i++) {
		RemoveConstraint(ExplorationConstraints[i]->Index);
	}
	ResetSolver();
	
	if (OriginalPrint) {
		PrintSolutions(-1,-1);
	}
	if (OriginalClear) {
		ClearSolutions();
	}
	InParameters->PrintSolutions = OriginalPrint;
	InParameters->ClearSolutions = OriginalClear;

	return Status;
}

int MFAProblem::MILPCoessentialityAnalysis(Data* InData, OptimizationParameter* InParameters, bool DoFindTightBounds, string &InNote, bool SubProblem) {
	//Notes on problem progress will be stored in this string
	InNote.append("Performing MILP coessentiality analysis(");

	//Building the problem from the model if it has not already been built
	if (FNumVariables() == 0) {
		if (BuildMFAProblem(InData,InParameters) != SUCCESS) {
			FErrorFile() << "Failed to build optimization problem." << endl;
			FlushErrorFile();
			InNote.append("Failed to build optimization problem");
			if (!SubProblem) {
				PrintProblemReport(FLAG,InParameters,InNote);
			}
			return FAIL;	
		}
	}

	//Initially maximizing and minimizing each reaction to classify the reactions
	if (DoFindTightBounds) {
		FindTightBounds(InData,InParameters,false,true);
	}

	//Finding the specified reactions in the database
	vector<vector<Reaction*> > TargetReactions;
	for (int i=0; i < int(InParameters->CoessentialityTargetReactions.size()); i++) {
		vector<Reaction*> ReactionArray;
		for (int j=0; j < int (InParameters->CoessentialityTargetReactions[i].size()); j++) {
			Reaction* TempReaction = InData->FindReaction("DATABASE;NAME",InParameters->CoessentialityTargetReactions[i][j].data());
			if (TempReaction != NULL) {
				ReactionArray.push_back(TempReaction);
			}
		}
		if (ReactionArray.size() > 0) {
			TargetReactions.push_back(ReactionArray);
		}
	}

	
	//Creating as an objective the sum of the use variables for all nonessential reactions
	LinEquation* OldObjective = ObjFunct;
	LinEquation* NewObjective = InitializeLinEquation();
	for (int i=0; i < InData->FNumReactions(); i++) {
		MFAVariable* TempVariable = InData->GetReaction(i)->GetMFAVar(FLUX);
		if (TempVariable != NULL) {
			if (TempVariable->Min < MFA_ZERO_TOLERANCE && TempVariable->Max > MFA_ZERO_TOLERANCE) {
				NewObjective->Variables.push_back(InData->GetReaction(i)->GetMFAVar(REACTION_USE));
				NewObjective->Coefficient.push_back(1);
			}
		}
		TempVariable = InData->GetReaction(i)->GetMFAVar(FORWARD_FLUX);
		if (TempVariable != NULL) {
			if (TempVariable->Min < MFA_ZERO_TOLERANCE && TempVariable->Max > MFA_ZERO_TOLERANCE) {
				NewObjective->Variables.push_back(InData->GetReaction(i)->GetMFAVar(FORWARD_USE));
				NewObjective->Coefficient.push_back(1);
			}
		}
		TempVariable = InData->GetReaction(i)->GetMFAVar(REVERSE_FLUX);
		if (TempVariable != NULL) {
			if (TempVariable->Min < MFA_ZERO_TOLERANCE && TempVariable->Max > MFA_ZERO_TOLERANCE) {
				NewObjective->Variables.push_back(InData->GetReaction(i)->GetMFAVar(REVERSE_USE));
				NewObjective->Coefficient.push_back(1);
			}
		}
	}
	ObjFunct = NewObjective;
	SetMin();
	LoadObjective();
	vector<int> VariableTypes;
	VariableTypes.push_back(OBJECTIVE_TERMS);

	//Scanning specified reactions in search of coessential reactions
	LinEquation* CoessentialObjective = InitializeLinEquation();
	for (int i=0; i < int(TargetReactions.size()); i++) {
		vector<vector<double> > OriginalUpperBounds;
		vector<vector<double> > OriginalLowerBounds;
		vector<vector<MFAVariable*> > AllVariables;
		vector<double> TempUpperBound;
		vector<double> TempLowerBound;
		vector<MFAVariable*> TempVariables;
		for (int j=0; j < int (TargetReactions[i].size()); j++) {
			//Turning on each reaction
			MFAVariable* TempVariable = TargetReactions[i][j]->GetMFAVar(FLUX);
			if (TempVariable != NULL) {
				TempVariables.push_back(TempVariable);
				TempUpperBound.push_back(TempVariable->UpperBound);
				TempLowerBound.push_back(TempVariable->LowerBound);
				TempVariable->LowerBound = 0;
				TempVariable->UpperBound = 0;
				LoadVariable(TempVariable->Index);
			}
			TempVariable = TargetReactions[i][j]->GetMFAVar(FORWARD_FLUX);
			if (TempVariable != NULL) {
				TempVariables.push_back(TempVariable);
				TempUpperBound.push_back(TempVariable->UpperBound);
				TempLowerBound.push_back(TempVariable->LowerBound);
				TempVariable->LowerBound = 0;
				TempVariable->UpperBound = 0;
				LoadVariable(TempVariable->Index);
			}
			TempVariable = TargetReactions[i][j]->GetMFAVar(REVERSE_FLUX);
			if (TempVariable != NULL) {
				TempVariables.push_back(TempVariable);
				TempUpperBound.push_back(TempVariable->UpperBound);
				TempLowerBound.push_back(TempVariable->LowerBound);
				TempVariable->LowerBound = 0;
				TempVariable->UpperBound = 0;
				LoadVariable(TempVariable->Index);
			}
		}
		OriginalUpperBounds.push_back(TempUpperBound);
		OriginalLowerBounds.push_back(TempLowerBound);
		AllVariables.push_back(TempVariables);
		LinEquation* NewObjective = InitializeLinEquation();
		for (int j=0; j < int(AllVariables.size()); j++) {
			for (int k=0; k < int(AllVariables[j].size()); k++) {
				//Forcing the target reaction to be "on" and finding the minimal reaction sets
				ResetVariableMarks(false);
				AllVariables[j][k]->LowerBound = 0.00001;
				AllVariables[j][k]->UpperBound = 100;
				LoadVariable(AllVariables[j][k]->Index);
				int NumSolutions = RecursiveMILP(InData,InParameters,VariableTypes,false);
				if (NumSolutions > 0) {
					for (int m=0; m < NumSolutions; m++) {
						for (int n=0; n < int(NewObjective->Variables.size()); n++) {
							if (GetSolution(m)->SolutionData[NewObjective->Variables[n]->Index] < 0.5) {
								NewObjective->Variables[n]->Mark = true;
							} else {
								NewObjective->Variables[n]->Mark = false;
							}
						}
					}
					//Setting the objective to minimizing the addition of unused reactions to the solution
					CoessentialObjective->Variables.clear();
					CoessentialObjective->Coefficient.clear();
					for (int m=0; m < FNumVariables(); m++) {
						if (GetVariable(m)->Mark) {
							CoessentialObjective->Variables.push_back(GetVariable(m));
							CoessentialObjective->Coefficient.push_back(1);
						}
					}
					ObjFunct = CoessentialObjective;
					LoadSolver();
					LoadObjective();
					AllVariables[j][k]->LowerBound = 0;
					AllVariables[j][k]->UpperBound = 0;
					LoadVariable(AllVariables[j][k]->Index);
					int NewNumSolutions = RecursiveMILP(InData,InParameters,VariableTypes,false);
					//Scanning the solution and saving all coessential reaction sets identified
					if (NewNumSolutions > 0) {
						if (AllVariables[j][k]->Type == REVERSE_USE) {
							InNote.append("-");
						}
						InNote.append(AllVariables[j][k]->AssociatedReaction->GetData("DATABASE",STRING));
						for (int m=NumSolutions; m < NewNumSolutions; m++) {
							string CurrentCoessentialSet;
							for (int n=0; n < int(CoessentialObjective->Variables.size()); n++) {
								if (GetSolution(m)->SolutionData[CoessentialObjective->Variables[n]->Index] > 0.5) {
									if (CurrentCoessentialSet.length() > 0) {
										CurrentCoessentialSet.append(",");
									}
									if (CoessentialObjective->Variables[n]->Type == REVERSE_USE) {
										CurrentCoessentialSet.append("-");
									}
									CurrentCoessentialSet.append(CoessentialObjective->Variables[n]->AssociatedReaction->GetData("DATABASE",STRING));
								}
							}
							if (CurrentCoessentialSet.length() > 0) {
								InNote.append("/");
								InNote.append(CurrentCoessentialSet);
							}
						}
						InNote.append("|");
					}
					//Resetting to the min reaction objective
					ObjFunct = NewObjective;
					LoadSolver();
					LoadObjective();
					//Clearing solutions
					ClearSolutions();
				} else {
					AllVariables[j][k]->LowerBound = 0;
					AllVariables[j][k]->UpperBound = 0;
					LoadVariable(AllVariables[j][k]->Index);
				}
			}

		}

		//Restoring the variables to their original bounds
		for (int j=0; j < int(AllVariables.size()); j++) {
			for (int k=0; k < int(AllVariables[j].size()); k++) {
				AllVariables[j][k]->LowerBound = OriginalLowerBounds[j][k];
				AllVariables[j][k]->UpperBound = OriginalUpperBounds[j][k];
				LoadVariable(AllVariables[j][k]->Index);
			}
		}
	}

	InNote.append(")");

	if (!SubProblem) {
		PrintProblemReport(FLAG,InParameters,InNote);
	}

	return SUCCESS;
}

int MFAProblem::RecursiveMILPStudy(Data* InData, OptimizationParameter* InParameters, bool DoFindTightBounds) {
	//Saving original print settings
	bool OriginalPrint = InParameters->PrintSolutions;
	bool OriginalClear = InParameters->ClearSolutions;
	InParameters->PrintSolutions = true;
	InParameters->ClearSolutions = true;
	int Status = SUCCESS;

	//First I build the problem from the model if it has not already been built
	string OriginalNote("Identifying type 3 pathways:");
	if (FNumVariables() == 0) {
		if (BuildMFAProblem(InData,InParameters) != SUCCESS) {
			FErrorFile() << "Failed to build optimization problem." << endl;
			FlushErrorFile();
			OriginalNote.append("Failed to build optimization problem");
			PrintProblemReport(FLAG,InParameters,OriginalNote);
			return FAIL;	
		}
	}
		
	//Saving the original objective
	LinEquation* OriginalObjective = ObjFunct;
	ObjFunct = NULL;

	//Clearing any existing solutions
	ClearSolutions();

	//Calculating tightbounds
	vector<double> OriginalMax;
	vector<double> OriginalMin;
	if (DoFindTightBounds) {
		FindTightBounds(InData,InParameters,false,true);
		for (int i=0; i < FNumVariables(); i++) {
			OriginalMax.push_back(GetVariable(i)->UpperBound);
			OriginalMin.push_back(GetVariable(i)->LowerBound);
			GetVariable(i)->UpperBound = GetVariable(i)->Max;
			GetVariable(i)->LowerBound = GetVariable(i)->Min;
		}
	}

	//Running recursive MILP
	int NumSolutions = RecursiveMILP(InData,InParameters,InParameters->RecursiveMILPTypes,true);

	//Restoring original bounds
	if (DoFindTightBounds) {
		for (int i=0; i < FNumVariables(); i++) {
			GetVariable(i)->UpperBound = OriginalMax[i];
			GetVariable(i)->LowerBound = OriginalMin[i];
		}
	}

	//Restoring original objective
	ObjFunct = OriginalObjective;
	if (ObjFunct == NULL) {
		ConvertStringToObjective(GetParameter("objective"),InData);
	}
	Status = LoadSolver(false);

	//Restoring the printing parameter
	InParameters->PrintSolutions = OriginalPrint;
	InParameters->ClearSolutions = OriginalClear;

	return Status;
}

int MFAProblem::IdentifyReactionLoops(Data* InData, OptimizationParameter* InParameters) {
	//Building the problem from the model if it has not already been built
	if (FNumVariables() == 0) {
		if (BuildMFAProblem(InData,InParameters) != SUCCESS) {
			FErrorFile() << "Failed to build optimization problem." << endl;
			FlushErrorFile();
			string Note("Failed to build optimization problem");
			PrintProblemReport(FLAG,InParameters,Note);
			return FAIL;	
		}
	}

	//Saving the problem state so it can be restored later
	int ProblemState = SaveState();	

	//First running tightbounds to identify all reactions that are unbound by constraints
	//Marking all fluxes, which are the variables I need tight bounds for.
	for (int i=0; i< FNumVariables(); i++) {
		GetVariable(i)->Mark = false;
		if (GetVariable(i)->Type == FLUX || GetVariable(i)->Type == FORWARD_FLUX || GetVariable(i)->Type == REVERSE_FLUX) {
			GetVariable(i)->Mark = true;
		}
	}

	//Finding tight bounds
	FindTightBounds(InData,InParameters,false);

	//Searching for the reactions that are unbound by the constraints
	for (int i=0; i< FNumVariables(); i++) {
		GetVariable(i)->Mark = false;
	}
	ClearSolutions();
	for (int i=0; i < FNumVariables(); i++) {
		if ((GetVariable(i)->Type == FLUX || GetVariable(i)->Type == FORWARD_FLUX || GetVariable(i)->Type == REVERSE_FLUX) && !GetVariable(i)->Mark && GetVariable(i)->Max > MFA_ZERO_TOLERANCE) {
			//Saving the original lower bound
			double OriginalBound = GetVariable(i)->LowerBound;
			GetVariable(i)->LowerBound = GetVariable(i)->Max;
			
			//Running recursive MILP
			vector<int> Types;
			Types.push_back(REACTION_USE);
			Types.push_back(FORWARD_USE);
			Types.push_back(REVERSE_USE);
			int NumSolutions = RecursiveMILP(InData,InParameters,Types,true);

			//Restoring the original lower bound
			GetVariable(i)->LowerBound = OriginalBound;
		}
	}

	//Restoring original objective
	LoadState(ProblemState,true,true,true,false,true);
	ClearState(ProblemState);
	LoadSolver();

	return SUCCESS;
}

int MFAProblem::LoadBiomassDrainReactions(Data* InData, OptimizationParameter* InParameters) {
	//Loading reactions supplying biomass components for biomass hypothesis
	if (GetParameter("Biomass modification hypothesis").compare("1") == 0) {
		if (verbose()) {
			cout << "Loading Biomass component reactions\n";
		}
		GetStringDB()->loadDatabaseTable("biomassComponentRxn","SINGLEFILE","id",FOutputFilepath()+"BiomassHypothesisEquations.txt","","\t","|",StringToStrings("id","|",false),true);
		StringDBTable* rxntbl = GetStringDB()->get_table("biomassComponentRxn");
		if (rxntbl == NULL) {
			return FAIL;
		}
		for (int i=0; i < rxntbl->number_of_objects();i++) {
			StringDBObject* rxnobj = rxntbl->get_object(i);
			Reaction* NewReaction = new Reaction(rxnobj->get("id"),rxnobj->get("equation"),rxnobj->get("name"),InData);
			NewReaction->AddData("FOREIGN","BiomassRxn",STRING);
			NewReaction->SetType(FORWARD);
			InData->AddReaction(NewReaction);
		}
	}
	return SUCCESS;
}

int MFAProblem::LoadGapFillingReactions(Data* InData, OptimizationParameter* InParameters) {
	if (InData->GetData("Reaction list loaded",STRING).length() == 0) {
		InData->AddData("Reaction list loaded","YES",STRING);
		vector<string>* AllowedUnbalancedReactions = StringToStrings(GetParameter("Allowable unbalanced reactions"),",");
		//Getting dissapproved compartment list
		vector<string>* DissapprovedCompartments = NULL;
		if (GetParameter("dissapproved compartments").compare("none") != 0) {
			DissapprovedCompartments = StringToStrings(GetParameter("dissapproved compartments"),";");
		}
		//Iterating through the list and loading any reaction that is not already present in the model		
		StringDBTable* rxntbl = GetStringDB()->get_table("reaction");
		if (rxntbl == NULL) {
			return FAIL;
		}
		if (verbose()) {
			cout << "Loading GapFillingReactions\n";
		}
		for (int i=0; i < rxntbl->number_of_objects();i++) {
			StringDBObject* rxnobj = rxntbl->get_object(i);
			string RxnId = rxnobj->get("id");
			//Making sure the reaction is not on the KO list
			bool AddReaction = true;
			for (int j=0; j < int(InParameters->KOReactions.size()); j++) {
				if (RxnId.compare(InParameters->KOReactions[j]) == 0) {
					AddReaction = false;
					break;
				}
			}

			//Test to see if reaction is not in model
			if (AddReaction && InData->FindReaction("DATABASE",RxnId.data()) != NULL) {
				AddReaction = false;
			}

			//Test status for OK flag
			if(AddReaction && rxnobj->get("status").length() >= 2 && GetParameter("Balanced reactions in gap filling only").compare("0") != 0) {
			  string stat = rxnobj->get("status").substr(0,2);
			  //RC, FO, SP, UN and RO statuses (Reversibility corrections and spontaneous reactions) are always accompanied by an "OK" status
			  if(! (stat.compare("OK") == 0 || stat.compare("RC") == 0 || stat.compare("FO") == 0 || stat.compare("RO") == 0 || stat.compare("SP") == 0 || stat.compare("UN") == 0) ) {
			    cout << "Status for reaction " << RxnId << " not OK: " << rxnobj->get("status") << endl;
			    AddReaction = false;
			    for (int j=0; j < int(AllowedUnbalancedReactions->size()); j++) {
					if (RxnId.compare((*AllowedUnbalancedReactions)[j]) == 0) {
						AddReaction = true;
						break;
					}
				}
			  }
			}

			if(AddReaction){
				Reaction* NewReaction = new Reaction(RxnId,InData);
				//Checking that only approved compartments are involved in the reaction
				bool ContainsDissapprovedCompartments = false;
				if (DissapprovedCompartments != NULL) {
					for (int j=0; j < NewReaction->FNumReactants(); j++) {
						for (int k=0; k < int(DissapprovedCompartments->size()); k++) {
							if ((*DissapprovedCompartments)[k].compare(GetCompartment(NewReaction->GetReactantCompartment(j))->Abbreviation) == 0) {
								ContainsDissapprovedCompartments = true;
							}
						}
					}
				}
				
				if (!ContainsDissapprovedCompartments) {
					//Checking if the reaction is balanced
					//if (!NewReaction->BalanceReaction(false,false)) {
					//	NewReaction->AddData("UNBALANCED","YES",STRING);
					//}
					if (GetParameter("Balanced reactions in gap filling only").compare("0") == 0 || NewReaction->GetData("UNBALANCED",STRING).length() == 0) {
						NewReaction->AddData("FOREIGN","Reaction",STRING);
						//NewReaction->SetType(NewReaction->CalculateDirectionalityFromThermo());
						InData->AddReaction(NewReaction);
					} else {
						for (int j=0; j < int(AllowedUnbalancedReactions->size()); j++) {
							if (NewReaction->GetData("DATABASE",STRING).compare((*AllowedUnbalancedReactions)[j]) == 0) {
								NewReaction->AddData("FOREIGN","Reaction",STRING);
								//NewReaction->SetType(NewReaction->CalculateDirectionalityFromThermo());
								InData->AddReaction(NewReaction);
								break;
							}
						}
					}
				}
				if (InData->FindReaction("DATABASE",NewReaction->GetData("DATABASE",STRING).data()) == NULL) {
					delete NewReaction;
				}
			}
		}
		delete AllowedUnbalancedReactions;
		if (DissapprovedCompartments != NULL) {
			delete DissapprovedCompartments;
		}
	}
	return SUCCESS;
}

int MFAProblem::CompleteGapFilling(Data* InData, OptimizationParameter* InParameters,bool fastgapfill) {
	double start = double(time(NULL));
	//Loading list of inactive reactions in model
	vector<float> InitialInactiveCoefficients;
	vector<string> InitialInactiveReactions = ReadStringsFromFile(FOutputFilepath()+"InactiveModelReactions.txt",false);
	map<string,Reaction*,std::less<string> > InitialInactiveVar;
	for (int i=0; i < int(InitialInactiveReactions.size()); i++) {
		vector<string>* strings = StringToStrings(InitialInactiveReactions[i],"\t");
		if (strings->size() >= 1) {
			InitialInactiveReactions[i] = (*strings)[0];
		}
		if (strings->size() >= 2) {
			InitialInactiveCoefficients.push_back(atof((*strings)[1].data()));
		} else {
			InitialInactiveCoefficients.push_back(1);
		}
		delete strings;
		Reaction* tempRxn = InData->FindReaction("DATABASE",InitialInactiveReactions[i].data());
		if (tempRxn != NULL) {
			InitialInactiveVar[InitialInactiveReactions[i]] = tempRxn;
		}
	}
	//Clearing the problem if it already exists
	if (FNumVariables() > 0) {
		ClearConstraints();	
		ClearVariables();
		ClearObjective();
	}
	ClearSolutions();
	//First we load the complete reaction list from file
	//this->LoadBiomassDrainReactions(InData,InParameters);
	if (GetParameter("Add DB reactions for gapfilling").compare("1") == 0) {
		if (this->LoadGapFillingReactions(InData,InParameters) != SUCCESS) {
			return FAIL;
		}
	}
	//Setting up initial problem to identify unfixable reactions
	InParameters->ReactionsUse = false;
	InParameters->AllReactionsUse = false;
	if (GetParameter("Make all reactions reversible in MFA").compare("1") == 0) {
		InParameters->AllReversible = true;
	}
	InParameters->DecomposeReversible = false;
	if (BuildMFAProblem(InData,InParameters) != SUCCESS) {
		FErrorFile() << "Failed to build optimization problem." << endl;
		FlushErrorFile();
		return FAIL;	
	}
	LinEquation* constraint = InitializeLinEquation("Forcing inactive reaction to be active",0.000001,GREATER,LINEAR);
	this->AddObjective(constraint);
	this->SetMax();
	vector<string> InactiveReactions;
	vector<int> Repaired;
	vector<string> FailedReactions;
	ofstream output;
	if (OpenOutput(output,(FOutputFilepath()+"CompleteGapfillingOutput.txt").data())) {
		output << "Target reaction\tGapfilled reactions\tActivated reactions\tCount\tTime\tRound repaired" << endl;
		output.close();
	}
	bool simultaneous = false;
	string primary_rxn;
	LinEquation* primaryconstraint = NULL;
	if (GetParameter("Simultaneous gapfill").compare("1") == 0) {
		simultaneous = true;
	}
	map<string,MFAVariable*,std::less<string> > InactiveSlackVar;
	map<string,double> InactiveVarCoef;
	for (int i=0; i < int(InitialInactiveReactions.size()); i++) {
		if (InitialInactiveReactions[i].length() > 0) {
			constraint->Variables.clear();
			constraint->Coefficient.clear();
			if (InitialInactiveReactions[i].length() > 3 && InitialInactiveReactions[i].substr(0,3).compare("drn") == 0) {
				string id = InitialInactiveReactions[i].substr(3);
				for (int j =0; j < this->FNumVariables(); j++) {
					if (this->GetVariable(j)->AssociatedSpecies != NULL && this->GetVariable(j)->AssociatedSpecies->GetData("DATABASE",STRING).compare(id) == 0 && this->GetVariable(j)->Compartment == GetCompartment("c")->Index) {
						if (this->GetVariable(j)->Type == REVERSE_DRAIN_FLUX) {
							constraint->Variables.push_back(this->GetVariable(j));
							constraint->Coefficient.push_back(1);
						}
					}
				}
			} else {
				MFAVariable* currVar = InitialInactiveVar[InitialInactiveReactions[i]]->GetMFAVar(FLUX);
				if (currVar != NULL) {
					constraint->Variables.push_back(currVar);
					constraint->Coefficient.push_back(1);
				}
				currVar = InitialInactiveVar[InitialInactiveReactions[i]]->GetMFAVar(FORWARD_FLUX);
				if (currVar != NULL) {
					constraint->Variables.push_back(currVar);
					constraint->Coefficient.push_back(1);
				}
				currVar = InitialInactiveVar[InitialInactiveReactions[i]]->GetMFAVar(REVERSE_FLUX);
				if (currVar != NULL) {
					constraint->Variables.push_back(currVar);
					constraint->Coefficient.push_back(1);
				}
			}
			if (i == 0) {
				this->ResetSolver();
				this->LoadSolver();
			} else {
				this->LoadObjective();
			}
			OptSolutionData* solution = RunSolver(false,false,false);
			if (solution->Status == SUCCESS && solution->Objective < MFA_ZERO_TOLERANCE) {
				//Attempt to maximize flux through the reaction in the reverse direction
				constraint->Coefficient[0] = -1;
				this->LoadObjective();
				solution = RunSolver(false,false,false);
			}
			if (solution->Status == SUCCESS && solution->Objective < MFA_ZERO_TOLERANCE) {
				string noGrowth("");
				if (InitialInactiveReactions[i].substr(0,3).compare("bio") == 0) {
					string note;
					CheckIndividualMetaboliteProduction(InData,InParameters,GetParameter("metabolites to optimize"),false,false,note,true);
					noGrowth = GetParameter("No growth metabolites");
					this->LoadSolver();
				}
				string tmpNote;
				FailedReactions.push_back(InitialInactiveReactions[i]);
				if (OpenOutput(output,(FOutputFilepath()+"CompleteGapfillingOutput.txt").data(),true)) {
					cout << InitialInactiveReactions[i] << "\tFAILED\tNONE\tPrelim\t" << (time(NULL)-start) << "\t--\t" << noGrowth << endl;
					output << InitialInactiveReactions[i] << "\tFAILED\tNONE\tPrelim\t" << (time(NULL)-start) << "\t--\t" << noGrowth << endl;
					output.close();
				}	
			} else {
				Repaired.push_back(-1);
				InactiveReactions.push_back(InitialInactiveReactions[i]);
				InactiveVarCoef[InitialInactiveReactions[i]] = InitialInactiveCoefficients[i];
			}
		}
	}
	//Setting up the gapfilling problem
	delete constraint;
	ObjFunct = NULL;
	ClearConstraints();	
	ClearVariables();
	ClearObjective();
	ClearSolutions();
	if (!fastgapfill || simultaneous) {
		InParameters->ReactionsUse = true;
		InParameters->AllReactionsUse = true;
	}
	if (GetParameter("Make all reactions reversible in MFA").compare("1") == 0) {
		InParameters->AllReversible = true;
	}
	InParameters->DecomposeReversible = true;
	double minimumFlux = atof(GetParameter("Minimum flux for use variable positive constraint").data());
	//Building the problem from the model
	if (BuildMFAProblem(InData,InParameters) != SUCCESS) {
		FErrorFile() << "Failed to build optimization problem." << endl;
		FlushErrorFile();
		return FAIL;	
	}
	//Loading objective coefficients or calculating from parameters
	map<string,Reaction*,std::less<string> > InactiveVar;
	map<Reaction*,int,std::less<Reaction*> > InactiveIndecies;
	for (int i=0; i < int(InactiveReactions.size()); i++) {
		Reaction* tempRxn = InData->FindReaction("DATABASE",InactiveReactions[i].data());
		if (tempRxn != NULL) {
			InactiveVar[InactiveReactions[i]] = tempRxn;
			InactiveIndecies[tempRxn] = i;
		}
		if (simultaneous) {
			string name = InactiveReactions[i];
			name += " activation constraint!";
			cout << name << endl;
			LinEquation* newconstraint = InitializeLinEquation(name.data(),0.01,GREATER,LINEAR);
			MFAVariable* currVar = tempRxn->GetMFAVar(FLUX);
			if (currVar != NULL) {
				newconstraint->Variables.push_back(currVar);
				newconstraint->Coefficient.push_back(1);
			}
			currVar = tempRxn->GetMFAVar(FORWARD_FLUX);
			if (currVar != NULL) {
				newconstraint->Variables.push_back(currVar);
				newconstraint->Coefficient.push_back(1);
			}
			currVar = tempRxn->GetMFAVar(REVERSE_FLUX);
			if (currVar != NULL) {
				newconstraint->Variables.push_back(currVar);
				newconstraint->Coefficient.push_back(1);
			}
			if (InactiveReactions[i].substr(0,3).compare("bio") != 0) {
				MFAVariable* NewVariable = InitializeMFAVariable();
				NewVariable->Type = REACTION_SLACK;
				this->AddVariable(NewVariable);
				NewVariable->Name = InactiveReactions[i]+" slack";
				NewVariable->AssociatedReaction = tempRxn;
				InactiveSlackVar[InactiveReactions[i]] = NewVariable;
				NewVariable->UpperBound = 1;
				NewVariable->LowerBound = 0;
				newconstraint->Variables.push_back(NewVariable);
				newconstraint->Coefficient.push_back(0.01);
				if (newconstraint->Variables.size() > 2 && fastgapfill) {
					name = InactiveReactions[i];
					name += " reversibility constraint";
					//LinEquation* quadconst = InitializeLinEquation(name.data(),0,EQUAL,QUADRATIC);
					//quadconst->QuadOne.push_back(newconstraint->Variables[0]);
					//quadconst->QuadTwo.push_back(newconstraint->Variables[1]);
					//quadconst->QuadCoeff.push_back(1);
					//this->AddConstraint(quadconst);
				}
			} else {
				primary_rxn = InitialInactiveReactions[i];
				primaryconstraint = newconstraint;
				newconstraint->RightHandSide = 0.1;
			}
			this->AddConstraint(newconstraint);
		}
	}
	map<MFAVariable*,double,std::less<MFAVariable*> > VariableCoefficients;
	if (this->CalculateGapfillCoefficients(InData,InParameters,InactiveVar,VariableCoefficients,fastgapfill) != SUCCESS) {
		FErrorFile() << "Failed to calculate reaction coefficients" << endl;
		FlushErrorFile();
		return FAIL;
	}
	//Creating objective function
	LinEquation* oldObjective = InitializeLinEquation("Gapfilling objective");
	vector<vector<int> > InactiveObjectiveIndecies(InactiveReactions.size());
	for (map<MFAVariable*,double,std::less<MFAVariable*> >::iterator mapIT = VariableCoefficients.begin(); mapIT != VariableCoefficients.end(); mapIT++) {
		cout << mapIT->second << " " << mapIT->first->AssociatedReaction->GetData("DATABASE",STRING) << " " << ConvertVariableType(mapIT->first->Type) << endl;
		oldObjective->Variables.push_back(mapIT->first);
		oldObjective->Coefficient.push_back(mapIT->second);
		if (mapIT->second < 0) {
			if (InactiveIndecies.count(mapIT->first->AssociatedReaction) > 0) {
				InactiveObjectiveIndecies[InactiveIndecies[mapIT->first->AssociatedReaction]].push_back(oldObjective->Variables.size()-1);
			}
			LinEquation* UseVarConstraint = CreateUseVariablePositiveConstraint(mapIT->first,InParameters);
			if (UseVarConstraint != NULL) {
				AddConstraint(UseVarConstraint);
			}
		}
	}
	this->AddObjective(oldObjective);
	this->SetMin();
	if (InParameters->labels.size() > 0) {
		Reaction* temprxn = InactiveVar[InactiveReactions[0]];
		LinEquation* newconstraint = InitializeLinEquation(InactiveReactions[0].data(),0.1,GREATER,LINEAR);
		MFAVariable* currVar = temprxn->GetMFAVar(FLUX);
		if (currVar != NULL) {
			newconstraint->Variables.push_back(currVar);
			newconstraint->Coefficient.push_back(1);
		}
		currVar = temprxn->GetMFAVar(FORWARD_FLUX);
		if (currVar != NULL) {
			newconstraint->Variables.push_back(currVar);
			newconstraint->Coefficient.push_back(1);
		}
		currVar = temprxn->GetMFAVar(REVERSE_FLUX);
		if (currVar != NULL) {
			newconstraint->Variables.push_back(currVar);
			newconstraint->Coefficient.push_back(1);
		}
		AddConstraint(newconstraint);
		this->RunDeletionExperiments(InData,InParameters,true);
	} else if (simultaneous) {
		double activationCoef = atof(GetParameter("Reaction activation bonus").data());
		for (map<string,MFAVariable*,std::less<string> >::iterator mapIT = InactiveSlackVar.begin(); mapIT != InactiveSlackVar.end(); mapIT++) {
			oldObjective->Variables.push_back(mapIT->second);
			oldObjective->Coefficient.push_back(InactiveVarCoef[mapIT->first]*activationCoef);
		}
		bool stay_in_loop = true;
		vector<string>* SolutionArray = new vector<string>;
		int currentround = 0;
		int count = 0;
		while (stay_in_loop) {
			string activated("");
			string gapfilled("");
			this->ResetSolver();
			this->LoadSolver();
			GlobalWriteLPFile(Solver,lpcount);
			OptSolutionData* solution = RunSolver(true,false,false);
			if (currentround == 0) {
				PrintSolutions(-1,-1,false);
			}
			double cost = 0;
			if (solution->Status == SUCCESS && solution->Objective > MFA_ZERO_TOLERANCE) {
				for (int j=0; j < int(oldObjective->Variables.size()); j++) {
					if (oldObjective->Variables[j]->AssociatedReaction != NULL) {
						if (oldObjective->Coefficient[j] > 0) {
							string sign = "+";
							if (oldObjective->Variables[j]->Type == REVERSE_FLUX) {
								sign = "-";
							}
							if (solution->SolutionData[oldObjective->Variables[j]->Index] > MFA_ZERO_TOLERANCE && oldObjective->Variables[j]->Type != REACTION_SLACK) {
								oldObjective->Coefficient[j] = 0;
								cost = cost + oldObjective->Coefficient[j];
								if (gapfilled.length() > 0) {
									gapfilled.append(",");
								}
								gapfilled.append(sign);
								gapfilled.append(oldObjective->Variables[j]->AssociatedReaction->GetData("DATABASE",STRING));
							} else if (oldObjective->Variables[j]->Type == REACTION_SLACK && solution->SolutionData[oldObjective->Variables[j]->Index] <= MFA_ZERO_TOLERANCE) {
								oldObjective->Coefficient[j] = 0;
								cost = cost + oldObjective->Coefficient[j];
								count++;
								if (activated.length() > 0) {
									activated.append(",");
								}
								activated.append(sign);
								activated.append(oldObjective->Variables[j]->AssociatedReaction->GetData("DATABASE",STRING));
							}
						}
					}
				}
			}
			if (activated.length() == 0 && gapfilled.length() == 0) {
				stay_in_loop = false;
			} else {
				string SolutionString = dtoa(cost);
				SolutionString.append(":");
				SolutionString.append(gapfilled);
				SolutionArray->push_back(SolutionString);
				if (OpenOutput(output,(FOutputFilepath()+"CompleteGapfillingOutput.txt").data(),true)) {
					cout << primary_rxn << "\t" << gapfilled << "\t" << activated << "\t" << count << "/" << InactiveReactions.size() << "\t" << (time(NULL)-start) << "\t" << currentround << endl;
					output << primary_rxn << "\t" << gapfilled << "\t" << activated << "\t" << count << "/" << InactiveReactions.size() << "\t" << (time(NULL)-start) << "\t" << currentround << endl;
					output.close();
				}
				if (primaryconstraint != NULL) {
					primaryconstraint->RightHandSide = -10000;
					primaryconstraint = NULL;
					primary_rxn = "none";
				}
			}
			currentround++;
		}
	} else {
		//Creating forcing constraint
		constraint = InitializeLinEquation("Forcing inactive reaction to be active",minimumFlux,GREATER,LINEAR);
		this->AddConstraint(constraint);
		//Creating output structures
		map<MFAVariable*,bool,std::less<Reaction*> > AddedReactions;
		//Creating variable sets for objective and forcing constraint for each inactive object
		vector<vector<MFAVariable*> > InactiveVariables;
		vector<vector<double> > InactiveCoeficients;
		for (int i=0; i < int(InactiveReactions.size()); i++) {
			vector<MFAVariable*> newVariables;
			vector<double> newCoeficients;
			if (InactiveReactions[i].length() > 3 && InactiveReactions[i].substr(0,3).compare("drn") == 0) {
				string id = InactiveReactions[i].substr(3);
				for (int j =0; j < this->FNumVariables(); j++) {
					if (this->GetVariable(j)->AssociatedSpecies != NULL && this->GetVariable(j)->AssociatedSpecies->GetData("DATABASE",STRING).compare(id) == 0 && this->GetVariable(j)->Compartment == GetCompartment("c")->Index) {
						if (this->GetVariable(j)->Type == REVERSE_DRAIN_FLUX) {
							newVariables.push_back(this->GetVariable(j));
							newCoeficients.push_back(1);
						}
					}
				}
			} else {
				MFAVariable* currVar = InactiveVar[InactiveReactions[i]]->GetMFAVar(FLUX);
				if (currVar != NULL) {
					newVariables.push_back(currVar);
					newCoeficients.push_back(1);
				}
				currVar = InactiveVar[InactiveReactions[i]]->GetMFAVar(FORWARD_FLUX);
				if (currVar != NULL) {
					newVariables.push_back(currVar);
					newCoeficients.push_back(1);
				}
				currVar = InactiveVar[InactiveReactions[i]]->GetMFAVar(REVERSE_FLUX);
				if (currVar != NULL) {
					newVariables.push_back(currVar);
					newCoeficients.push_back(1);
				}
			}
			InactiveVariables.push_back(newVariables);
			InactiveCoeficients.push_back(newCoeficients);
		}
		//Looping over all inactive reactions in prioritized order
		int count = 0;
		bool firstSolution = false;
		for (int i=0; i < int(InactiveReactions.size()); i++) {
			//Gapfilling the inactive reaction if it is still nonfunctional
			string activated("");
			string gapfilled("");
			if (Repaired[i] == -1) {
				//Checking all reactions to determine if they are now functional
				ObjFunct = NULL;
				constraint->RightHandSide = -10000;
				this->AddObjective(constraint);
				this->SetMax();
				for (int j=0; j < int(oldObjective->Variables.size()); j++) {
					if (oldObjective->Coefficient[j] > 0) {
						oldObjective->Variables[j]->UpperBound = 0;
					}
				}
				bool first = true;
				for (int k=0; k < int(InactiveReactions.size()); k++) {
					if (Repaired[k] == -1) {
						constraint->Variables.clear();
						constraint->Coefficient.clear();
						for (int j=0; j < int(InactiveVariables[k].size()); j++) {
							constraint->Variables.push_back(InactiveVariables[k][j]);
							constraint->Coefficient.push_back(InactiveCoeficients[k][j]);
						}
						if (constraint->Variables.size() > 0) {
							if (first) {
								this->ResetSolver();
								this->LoadSolver();
								first = false;
							} else {
								this->LoadObjective();
							}
							OptSolutionData* solution = RunSolver(false,false,false);
							if (solution->Status == SUCCESS && solution->Objective > MFA_ZERO_TOLERANCE) {
								if (count == 0) {
									Repaired[k] = 0;
								} else {
									Repaired[k] = count-1;
								}
								//Zeroing out the inactive coefficient in the objective function
								for(int m=0; m < int(InactiveObjectiveIndecies[k].size()); m++) {
									oldObjective->Coefficient[InactiveObjectiveIndecies[k][m]] = 0;
								}
							}
							delete solution;
						}
					}
				}
				if (Repaired[i] == -1) {
					ObjFunct = NULL;
					this->AddObjective(oldObjective);
					this->SetMin();
					constraint->Variables.clear();
					constraint->Coefficient.clear();
					for (int j=0; j < int(InactiveVariables[i].size()); j++) {
						constraint->Variables.push_back(InactiveVariables[i][j]);
						constraint->Coefficient.push_back(InactiveCoeficients[i][j]);
					}
					constraint->RightHandSide = minimumFlux;
					for (int j=0; j < int(oldObjective->Variables.size()); j++) {
						if (oldObjective->Coefficient[j] > 0) {
							if (fastgapfill) {
								oldObjective->Variables[j]->UpperBound = 10000;
							} else {
								oldObjective->Variables[j]->UpperBound = 1;
							}
						}
					}
					this->ResetSolver();
					this->LoadSolver();
					if (GetParameter("just print LP file").compare("1") == 0) {
						WriteLPFile();
						return SUCCESS;
					}
					int TotalSolution = 1;
					vector<string>* SolutionArray = NULL;
					if (fastgapfill) {
						OptSolutionData* solution = RunSolver(false,false,false);
						SolutionArray = new vector<string>;
						if (solution->Status == SUCCESS && solution->Objective > MFA_ZERO_TOLERANCE) {
							string solutionstring(":");
							double cost = 0;
							for (int j=0; j < int(oldObjective->Variables.size()); j++) {
								if (solution->SolutionData[oldObjective->Variables[j]->Index] > MFA_ZERO_TOLERANCE) {
									cost = cost + oldObjective->Coefficient[j];
									if (solutionstring.size() > 1) {
										solutionstring.append(",");
									}
									if (oldObjective->Variables[j]->Type == REVERSE_FLUX) {
										solutionstring.append("-");
									} else {
										solutionstring.append("+");
									}
									solutionstring.append(oldObjective->Variables[j]->Name);
								}
							}
							solutionstring.insert(0,dtoa(cost));
							cout << solutionstring << endl;
							SolutionArray->push_back(solutionstring);
						}
					} else {
						vector<int> VariableTypes;
						VariableTypes.push_back(OBJECTIVE_TERMS);
						TotalSolution = RecursiveMILP(InData,InParameters,VariableTypes,true);
						if (TotalSolution > 0) {
							SolutionArray = StringToStrings(GetParameter("Current gap filling solutions"),"|");
						}

					}
					//Printing the gap filling output
					if (SolutionArray != NULL && SolutionArray->size() > 0) {
						//Parsing solution results
						string SolutionCosts;
						string Solutions;
						//Printing the run results
						double Cost = 0;
						for (int k=0; k < int(SolutionArray->size()); k++) {
							if ((*SolutionArray)[k].length() > 0) {
								vector<string>* SolutionDataArray = StringToStrings((*SolutionArray)[k],":");
								double Cost = atof((*SolutionDataArray)[0].data());
								if (Cost > MFA_ZERO_TOLERANCE && SolutionDataArray->size() > 1 && (*SolutionDataArray)[1].length() > 0) {
									if (gapfilled.length() > 0) {
										gapfilled.append("|");
									}
									gapfilled.append((*SolutionDataArray)[1]);
									map<Reaction*,bool,std::less<Reaction*> > activatedReactions;
									vector<string>* SolutionReactions = StringToStrings((*SolutionDataArray)[1],",");
									string currentGapfilling("");
									for (int j=0; j < int(SolutionReactions->size()); j++) {
										if (currentGapfilling.length() > 0) {
											currentGapfilling.append(";");
										}
										currentGapfilling.append((*SolutionReactions)[j]);
										// Zero out the objective coefficient for reactions added by gapfilling
										// There might be a more efficient way to get this index... but I want to make sure I'm zeroing out the right reaction
										// and the index for OldObjective is not the same as the index for SolutionReactions.
										//
										// Note - this probably wil break things if we are trying to get muleiple solutions to multiple inactive reactions.
										for ( int n=0; n < oldObjective->Variables.size(); n++ ) {
										  if ( oldObjective->Variables[n]->AssociatedReaction->GetData("DATABASE", STRING).compare((*SolutionReactions)[j]) == 0 ) {
											oldObjective->Coefficient[n] = 0;
											break;
										  }
										}
										//TODO: Handle activated reactions
										//activated.append(sign+oldObjective->Variables[j]->AssociatedReaction->GetData("DATABASE",STRING));
										//activatedReactions[oldObjective->Variables[j]->AssociatedReaction] = true;
									}
									delete SolutionReactions;
								}
								delete SolutionDataArray;
							}
						}
						delete SolutionArray;
						// zero out objective coefficients for the repaired reaction (mirroring what's done above if we mark a reaction as repared)
						for(int m=0; m < int(InactiveObjectiveIndecies[i].size()); m++) {
						  oldObjective->Coefficient[InactiveObjectiveIndecies[i][m]] = 0;
						}
						Repaired[i] = count;
					} else {
					  // Prevent repeatedly doing FBA after a failed gapfill.
					  Repaired[i] = -2;
						gapfilled = "FAILED";
						activated = "NONE";
					}
					count++;
				} else {
					gapfilled = "UNNEEDED";
					activated = "NONE";
				}
			} else {
				gapfilled = "UNNEEDED";
				activated = "NONE";
			}
			//Printing result for reaction
			if (OpenOutput(output,(FOutputFilepath()+"CompleteGapfillingOutput.txt").data(),true)) {
				cout << InactiveReactions[i] << "\t" << gapfilled << "\t" << activated << "\t" << count << "/" << InactiveReactions.size() << "\t" << (time(NULL)-start) << "\t" << Repaired[i] << endl;
				output << InactiveReactions[i] << "\t" << gapfilled << "\t" << activated << "\t" << count << "/" << InactiveReactions.size() << "\t" << (time(NULL)-start) << "\t" << Repaired[i] << endl;
				output.close();
				if (firstSolution && GetParameter("Solve complete gapfilling only once").compare("1") == 0) {
					i = int(InactiveReactions.size());
				}
			}
		}
	}
	ofstream outputTwo;
	if (OpenOutput(outputTwo,(FOutputFilepath()+"GapfillingComplete.txt").data())) {
		outputTwo << "Gapfilling completed:" << (time(NULL)-start) << endl;
		outputTwo.close();
	}
	ObjFunct = NULL;
	this->AddObjective(oldObjective);
	return SUCCESS;
}

int MFAProblem::CalculateGapfillCoefficients(Data* InData,OptimizationParameter* InParameters,map<string,Reaction*,std::less<string> > InactiveVar,map<MFAVariable*,double,std::less<MFAVariable*> >& VariableCoefficients,bool fastgapfill) {
	int forvar = FORWARD_USE;
	int revvar = REVERSE_USE;
	int univar = REACTION_USE;
	if (fastgapfill == true) {
		forvar = FORWARD_FLUX;
		revvar = REVERSE_FLUX;
		univar = FLUX;
	}
	map<MFAVariable*,double,std::less<MFAVariable*> > FileCoefficients;
	if (GetParameter("Objective coefficient file").compare("NONE") != 0 || GetParameter("Objective coefficient file").length() == 0) {
		vector<string> CoefficientList = ReadStringsFromFile(FOutputFilepath()+GetParameter("Objective coefficient file"),false);
		for (int i=1; i < int(CoefficientList.size()); i++) {
			vector<string>* strings = StringToStrings(CoefficientList[i],"\t");
			if (strings->size() >= 3) {
				if ((*strings)[0].compare("forward") == 0) {
					Reaction* currReaction = InData->FindReaction("DATABASE",(*strings)[1].data());
					if (currReaction != NULL) {
						MFAVariable* currVar = currReaction->GetMFAVar(forvar);
						if (currVar == NULL) {
							currVar = currReaction->GetMFAVar(univar);
						}
						if (currVar != NULL) {
							FileCoefficients[currVar] = atof((*strings)[2].data());
						}
					}
				} else if ((*strings)[0].compare("reverse") == 0) {
					Reaction* currReaction = InData->FindReaction("DATABASE",(*strings)[1].data());
					if (currReaction != NULL) {
						MFAVariable* currVar = currReaction->GetMFAVar(revvar);
						if (currVar != NULL) {
							FileCoefficients[currVar] = atof((*strings)[2].data());
						}
					}
				}
			}
			delete strings;
		}
		if (GetParameter("Use coefficient file exclusively").compare("1") == 0) {
			for (map<MFAVariable*,double,std::less<MFAVariable*> >::iterator mapIT = FileCoefficients.begin(); mapIT != FileCoefficients.end(); mapIT++) {
				VariableCoefficients[mapIT->first] = mapIT->second;
			}
			return SUCCESS;
		}
	}
	//Loading subsystem data from file
	map<string, double, std::less<string> > SubsystemReactions;
	map<string, double, std::less<string> > SubsystemModelReactions;
	//Loadining scenario data from file
	map<string, double, std::less<string> > ScenarioReactions;
	map<string, double, std::less<string> > ScenarioModelReactions;
	ifstream Input;
	if (OpenInput(Input,GetParameter("Function mapping filename"))) {
		GetFileLine(Input);
		do {
			vector<string>* Strings  = GetStringsFileline(Input,"\t");
			if (Strings->size() < 3 || (*Strings)[0].length() == 0 || (*Strings)[2].length() == 0) {
				delete Strings;
				break;
			}
			Reaction* SubsystemReaction = InData->FindReaction("DATABASE",(*Strings)[0].data());
			if (SubsystemReaction != NULL) {
				SubsystemReaction->AddData("FUNCTIONAL ROLE",(*Strings)[1].data(),STRING);
				if ((*Strings)[2].compare("NONE") != 0) {
					if (SubsystemReactions.count((*Strings)[2]) == 0) {
						SubsystemReactions[(*Strings)[2]] = 0;
					}
					SubsystemReactions[(*Strings)[2]]++;
					if (SubsystemReaction->GetData("FOREIGN",STRING).length() == 0) {
						if (SubsystemModelReactions.count((*Strings)[2]) == 0) {
							SubsystemModelReactions[(*Strings)[2]] = 0;
						}
						SubsystemModelReactions[(*Strings)[2]]++;
					}
					SubsystemReaction->AddData("SUBSYSTEMS",(*Strings)[2].data(),STRING);
				}
			}
			delete Strings;
		} while(!Input.eof());
		Input.close();
	}
	if (OpenInput(Input,GetParameter("Reaction database filename"))) {
		vector<string>* Strings  = GetStringsFileline(Input,"\t");
		int KeggMapColumn = int(FLAG);
		for (int i=0; i < int(Strings->size()); i++) {
			if ((*Strings)[i].compare("KEGG MAPS") == 0) {
				KeggMapColumn = i;
				break;
			}
		}
		delete Strings;
		do {
			Strings  = GetStringsFileline(Input,";");
			if (int(Strings->size()) > KeggMapColumn && (*Strings)[0].length() == 8 && (*Strings)[KeggMapColumn].length() > 0) {
				Reaction* SubsystemReaction = InData->FindReaction("DATABASE",(*Strings)[0].data());
				if (SubsystemReaction != NULL) {
					SubsystemReaction->AddData("KEGG MAPP",(*Strings)[KeggMapColumn].data(),STRING);
				}
			}
			delete Strings;
		} while(!Input.eof());
		Input.close();
	}
	if (OpenInput(Input,GetParameter("hope scenarios filename"))) {
		GetFileLine(Input);
		do {
			vector<string>* Strings  = GetStringsFileline(Input,"\t");
			if (Strings->size() == 0 || (*Strings)[0].length() == 0) {
				delete Strings;
				break;
			}
			if (ScenarioReactions.count((*Strings)[0]) == 0) {
				ScenarioReactions[(*Strings)[0]] = 0;
			}
			ScenarioReactions[(*Strings)[0]]++;
			if (Strings->size() >= 2 && (*Strings)[0].length() > 0 && (*Strings)[1].length() > 0) {
				Reaction* SubsystemReaction = InData->FindReaction("KEGG",(*Strings)[1].data());
				if (SubsystemReaction != NULL) {
					if (SubsystemReaction->GetData("FOREIGN",STRING).length() == 0) {
						if (ScenarioModelReactions.count((*Strings)[0]) == 0) {
							ScenarioModelReactions[(*Strings)[0]] = 0;
						}
						ScenarioModelReactions[(*Strings)[0]]++;
					}
					SubsystemReaction->AddData("SCENARIOS",(*Strings)[0].data(),STRING);
				}
			}
			delete Strings;
		} while(!Input.eof());
		Input.close();
	}

	//Marking all biomass components
	for (int i=0; i < InData->FNumReactions(); i++) {
		if (InData->GetReaction(i)->GetData("DATABASE",STRING).length() > 3 && InData->GetReaction(i)->GetData("DATABASE",STRING).substr(0,3).compare("bio") == 0) {
			for (int j=0; j < InData->GetReaction(i)->FNumReactants(REACTANT); j++) {
				InData->GetReaction(i)->GetReactant(j)->AddData("BIOMASS COMPONENT","YES",STRING);
			}
		}
	}

	//Separate database-related from thermo-related penalties. We only want probanno coefficients to affect the former.
	map<MFAVariable*,double,std::less<MFAVariable*> > ThermoPenalties;
	map<MFAVariable*,double,std::less<MFAVariable*> > DatabasePenalties;
	for (int i=0; i < InData->FNumReactions(); i++) {
		if (InData->GetReaction(i)->GetData("FOREIGN",STRING).compare("BiomassComp") == 0) {
			MFAVariable* TempVariable = InData->GetReaction(i)->GetMFAVar(forvar);
			if (TempVariable != NULL) {
				VariableCoefficients[TempVariable] = atof(GetParameter("Biomass component reaction penalty").data());
			}
		} else if (InData->GetReaction(i)->GetData("FOREIGN",STRING).length() > 0) {
			double Coefficient = 1;
			//Applying the non kegg reaction penalty
			if (InData->GetReaction(i)->GetData("KEGG",DATABASE_LINK).length() == 0) {
				InData->GetReaction(i)->SetData("PENALTY",(InData->GetReaction(i)->GetData("PENALTY",STRING)+"NO KEGG,").data(),STRING);
				Coefficient += atof(GetParameter("non KEGG reaction penalty").data());
			}
			//Applying the subsystem penalty and bonus
			if (InData->GetReaction(i)->GetData("SUBSYSTEMS",STRING).length() == 0) {
				InData->GetReaction(i)->SetData("PENALTY",(InData->GetReaction(i)->GetData("PENALTY",STRING)+"NO SUBSYSTEM,").data(),STRING);
				Coefficient += atof(GetParameter("no subsystem penalty").data());
			} else {
				//Applying the subsystem coverage bonus
				double BestCoverage = 0;
				vector<string> Subsystems = InData->GetReaction(i)->GetAllData("SUBSYSTEMS",STRING);
				for (int j=0;j < int(Subsystems.size()); j++) {
					double CurrentCoverage = SubsystemModelReactions[Subsystems[j]]/SubsystemReactions[Subsystems[j]];
					if (CurrentCoverage > BestCoverage && CurrentCoverage <= 1) {
						BestCoverage = CurrentCoverage;
					}
				}
				InData->GetReaction(i)->SetData("PENALTY",(InData->GetReaction(i)->GetData("PENALTY",STRING)+"SUBSYS COVERAGE,").data(),STRING);
				Coefficient += -BestCoverage*atof(GetParameter("subsystem coverage bonus").data());
			}
			//Applying the scenario coverage bonus
			if (InData->GetReaction(i)->GetData("SCENARIOS",STRING).length() > 0) {
				double BestCoverage = 0;
				vector<string> Subsystems = InData->GetReaction(i)->GetAllData("SCENARIOS",STRING);
				for (int j=0;j < int(Subsystems.size()); j++) {
					double CurrentCoverage = ScenarioModelReactions[Subsystems[j]]/ScenarioReactions[Subsystems[j]];
					if (CurrentCoverage > BestCoverage && CurrentCoverage <= 1) {
						BestCoverage = CurrentCoverage;
					}
				}
				InData->GetReaction(i)->SetData("PENALTY",(InData->GetReaction(i)->GetData("PENALTY",STRING)+"SCENARIO COVERAGE,").data(),STRING);
				Coefficient += -BestCoverage*atof(GetParameter("scenario coverage bonus").data());
			}
			//Applying no functional role penalty
			if (InData->GetReaction(i)->GetData("FUNCTIONAL ROLE",STRING).length() == 0) {
				InData->GetReaction(i)->SetData("PENALTY",(InData->GetReaction(i)->GetData("PENALTY",STRING)+"NO ROLE,").data(),STRING);
				Coefficient += atof(GetParameter("no functional role penalty").data());
			}
			//Applying no KEGG map penalty
			if (InData->GetReaction(i)->GetData("KEGG MAPP",STRING).length() == 0) {
				InData->GetReaction(i)->SetData("PENALTY",(InData->GetReaction(i)->GetData("PENALTY",STRING)+"NO KEGG MAP,").data(),STRING);
				Coefficient += atof(GetParameter("no KEGG map penalty").data());
			}
			//Applying the transporter penalty
			bool Transport = false;
			bool Stereo = false;
			InData->GetReaction(i)->CheckForTransportOrStereo(Transport,Stereo);
			if (Transport) {
				if (InData->GetReaction(i)->FNumReactants() == 1) {
					InData->GetReaction(i)->SetData("PENALTY",(InData->GetReaction(i)->GetData("PENALTY",STRING)+"SINGLE TRANSPORT,").data(),STRING);
					Coefficient += atof(GetParameter("single compound transporter penalty").data());
				}
				bool BiomassTransporter = false;
				for (int j=0; j < int(InData->GetReaction(i)->FNumReactants()); j++) {
					if (InData->GetReaction(i)->GetReactantCompartment(j) != GetDefaultCompartment()->Index && InData->GetReaction(i)->GetReactant(j)->GetData("BIOMASS COMPONENT",STRING).length() > 0) {
						BiomassTransporter = true;
						break;
					}
				}
				if (BiomassTransporter) {
					InData->GetReaction(i)->SetData("PENALTY",(InData->GetReaction(i)->GetData("PENALTY",STRING)+"BIOMASS TRANSPORT,").data(),STRING);
					Coefficient += atof(GetParameter("biomass transporter penalty").data());
				}
				InData->GetReaction(i)->SetData("PENALTY",(InData->GetReaction(i)->GetData("PENALTY",STRING)+"TRANSPORT,").data(),STRING);
				Coefficient += atof(GetParameter("transporter penalty").data());
			}
			//Applying the no thermo penalty
			double ThermodynamicPenalty = atof(GetParameter("directionality penalty").data());
			if (InData->GetReaction(i)->FEstDeltaG() == FLAG) {
				InData->GetReaction(i)->SetData("PENALTY",(InData->GetReaction(i)->GetData("PENALTY",STRING)+"NO DELTAG,").data(),STRING);
				Coefficient += atof(GetParameter("no delta G penalty").data());
			}
			if (InData->GetReaction(i)->FEstDeltaG() != FLAG) {
				ThermodynamicPenalty += fabs(InData->GetReaction(i)->FEstDeltaG())/10;
			} else {
				ThermodynamicPenalty += 1.5;
			}
			//Applying the missing structure penalty
			if (InData->GetReaction(i)->ContainsUnknownStructures() > 0) {
				InData->GetReaction(i)->SetData("PENALTY",(InData->GetReaction(i)->GetData("PENALTY",STRING)+"UNKNOWN STRUCTURE,").data(),STRING);
				Coefficient += atof(GetParameter("unknown structure penalty").data());
			}
			//Applying the unbalanced penalty
			if (InData->GetReaction(i)->GetData("UNBALANCED",STRING).length() > 0) {
				InData->GetReaction(i)->SetData("PENALTY",(InData->GetReaction(i)->GetData("PENALTY",STRING)+"UNBALANCED,").data(),STRING);
				Coefficient += atof(GetParameter("unbalanced penalty").data());
			}
			//Makeing sure the coefficient has a minimum value
			if (Coefficient < 0.5) {
				Coefficient = 0.5;
			}
			MFAVariable* TempVariable = InData->GetReaction(i)->GetMFAVar(forvar);
			if (TempVariable == NULL) {
				TempVariable = InData->GetReaction(i)->GetMFAVar(univar);
			}
			double ForwardPenalty = 0;
			double BackwardPenalty = 0;
			if (TempVariable != NULL) {
				DatabasePenalties[TempVariable] = Coefficient;
				if (InData->GetReaction(i)->GetDoubleData("ORIGINALDIR") == FORWARD || InData->GetReaction(i)->GetDoubleData("ORIGINALDIR") == REVERSIBLE) {
					ForwardPenalty = Coefficient;
					VariableCoefficients[TempVariable] = Coefficient;
					ThermoPenalties[TempVariable] = 0;
				} else {
					ForwardPenalty = Coefficient+ThermodynamicPenalty;
					VariableCoefficients[TempVariable] = Coefficient+ThermodynamicPenalty;
					ThermoPenalties[TempVariable] = ThermodynamicPenalty;
				}
			}
			TempVariable = InData->GetReaction(i)->GetMFAVar(revvar);
			if (TempVariable != NULL) {
				DatabasePenalties[TempVariable] = Coefficient;
				if (InData->GetReaction(i)->GetDoubleData("ORIGINALDIR") == FORWARD) {
					BackwardPenalty = Coefficient+ThermodynamicPenalty;
					VariableCoefficients[TempVariable] = Coefficient+ThermodynamicPenalty;
					ThermoPenalties[TempVariable] = ThermodynamicPenalty;
				} else {
					BackwardPenalty = Coefficient;
					VariableCoefficients[TempVariable] = Coefficient;
					ThermoPenalties[TempVariable] = 0;
				}
			}
			FLogFile() << InData->GetReaction(i)->GetData("DATABASE",STRING) << "\t" << InData->GetReaction(i)->GetData("PENALTY",STRING) << "\t" << ForwardPenalty << "\t" << BackwardPenalty << endl;
		} else if (InData->GetReaction(i)->GetData("BIOMASS DRAIN REACTION",STRING).compare("") == 0) {
			double ForwardPenalty = 0;
			double BackwardPenalty = 0;
			double ThermodynamicPenalty = atof(GetParameter("directionality penalty").data());
			if (InData->GetReaction(i)->FEstDeltaG() != FLAG) {
				ThermodynamicPenalty += fabs(InData->GetReaction(i)->FEstDeltaG())/10;
			} else {
				ThermodynamicPenalty += 1.5;
			}
			//if (InData->GetReaction(i)->FType() == FORWARD) {
				MFAVariable* TempVariable = InData->GetReaction(i)->GetMFAVar(revvar);
				if (TempVariable != NULL) {
					VariableCoefficients[TempVariable] = ThermodynamicPenalty;
					ForwardPenalty = ThermodynamicPenalty;
					ThermoPenalties[TempVariable] = ThermodynamicPenalty;
					DatabasePenalties[TempVariable] = 0;
				}
			//} else if (InData->GetReaction(i)->FType() == REVERSE) {
				TempVariable = InData->GetReaction(i)->GetMFAVar(forvar);
				if (TempVariable != NULL) {
					VariableCoefficients[TempVariable] = ThermodynamicPenalty;
					BackwardPenalty = ThermodynamicPenalty;
					ThermoPenalties[TempVariable] = ThermodynamicPenalty;
					DatabasePenalties[TempVariable] = 0;
				}
			//}
		}
		if (InParameters->ThermoConstraints && InParameters->ReactionErrorUseVariables) {
			MFAVariable* TempVariable = InData->GetReaction(i)->GetMFAVar(SMALL_DELTAG_ERROR_USE);
			if (TempVariable != NULL) {
				VariableCoefficients[TempVariable] = 1;
				ThermoPenalties[TempVariable] = 1;
				DatabasePenalties[TempVariable] = 0;
			}
			TempVariable = InData->GetReaction(i)->GetMFAVar(LARGE_DELTAG_ERROR_USE);
			if (TempVariable != NULL) {
				VariableCoefficients[TempVariable] = 10;
				ThermoPenalties[TempVariable] = 10;
				DatabasePenalties[TempVariable] = 0;
			}
		}
	}
	for (map<MFAVariable*,double,std::less<MFAVariable*> >::iterator mapIT = FileCoefficients.begin(); mapIT != FileCoefficients.end(); mapIT++) {
		if ( VariableCoefficients.find(mapIT->first) != VariableCoefficients.end() ) {
		  if ( DatabasePenalties.find(mapIT->first) != DatabasePenalties.end() && ThermoPenalties.find(mapIT->first) != ThermoPenalties.end() ) {
			VariableCoefficients[mapIT->first] = mapIT->second * DatabasePenalties[mapIT->first] + ThermoPenalties[mapIT->first];
		  }
		}
	}
	return SUCCESS;
}	

int MFAProblem::GapFilling(Data* InData, OptimizationParameter* InParameters,OptSolutionData*& CurrentSolution) {
	//Saving the old objective
	SavedBounds* bounds = this->saveBounds();
	LinEquation* OldObjective = GetObjective();
	this->ObjectiveConstraint->RightHandSide = InParameters->MinimumTargetFlux;
	ObjFunct = NULL;
	bool originalsense = this->FMax();
	double omegap = 1-InParameters->omega;
	if (InParameters->omega > 0) {
		ObjFunct = CloneLinEquation(OldObjective);
		int sign = 1;
		if (originalsense) {
			sign = -1;
		}
		//Rescaling the old object coefficients by the maximum objective value
		for (int i=0; i < OldObjective->Variables.size(); i++) {
			ObjFunct->Coefficient[i] = ObjFunct->Coefficient[i]*sign*InParameters->omega/CurrentSolution->Objective;
		}
	} else {
		ObjFunct = InitializeLinEquation("Gapfilling objective");
	}

	// apply MaxPenalty to entire collection of high and low expression penalty terms
	int startsize = int(ObjFunct->Variables.size());
	double penaltysum = 0;
	double alphap = 1-InParameters->alpha;
	if (InParameters->alpha > 0) {
		//Reading inactive coefficients
		vector< vector<string> >* rows = LoadMultipleColumnFile(FOutputFilepath()+"ActivationCoefficients.txt","\t");
		cout << "Processing ActivationCoefficients.txt" << endl;
		for (int i=1; i < int(rows->size()); i++) {
			cout << (*rows)[i][0].data();
			Reaction* CurrentRxn = InData->FindReaction("DATABASE",(*rows)[i][0].data());
			if (CurrentRxn != NULL && ! CurrentRxn->IsBiomassReaction()) {
			  MFAVariable* newvar = CurrentRxn->GetMFAVar(REACTION_SLACK);
			  if (newvar != NULL) {
			    ObjFunct->Variables.push_back(newvar);
			    double penalty = atof((*rows)[i][1].data())*InParameters->alpha;
			    penaltysum += penalty;
			    ObjFunct->Coefficient.push_back(penalty);
			    cout << " pushed REACTION_SLACK with penalty " << penalty << endl;
			  }
			}
			else {
				cout << endl;
			}
		}
	}
	vector<int> variables;
	variables.push_back(FLUX);
	variables.push_back(FORWARD_FLUX);
	variables.push_back(REVERSE_FLUX);
	if (InParameters->ReactionsUse) {
		variables[0] = REACTION_USE;
		variables[1] = FORWARD_USE;
		variables[2] = REVERSE_USE;
	}
	if (InParameters->alpha < 1) {
		// compute max flux
		double maxbound = 0;
		for (int i=0; i < FNumVariables(); i++) {
			if (GetVariable(i)->AssociatedReaction != NULL && (GetVariable(i)->Type == FORWARD_FLUX || GetVariable(i)->Type == REVERSE_FLUX || GetVariable(i)->Type == FLUX)) {
				if (GetVariable(i)->Max > maxbound) {
					maxbound = GetVariable(i)->Max;
				}
			}
		}
	  
		//Reading gapfilling coefficients
		map<MFAVariable*,double,std::less<MFAVariable*> > BaseCoefficients;
		map<string,Reaction*,std::less<string> > InactiveVar;
		CalculateGapfillCoefficients(InData,InParameters,InactiveVar,BaseCoefficients,!InParameters->ReactionsUse);
		vector< vector<string> >* rows = LoadMultipleColumnFile(FOutputFilepath()+"GapfillingCoefficients.txt","\t");
		cout << "Processing GapfillingCoefficients.txt" << endl;
		for (int i=1; i < int(rows->size()); i++) {
			Reaction* CurrentRxn = InData->FindReaction("DATABASE",(*rows)[i][0].data());
			cout << (*rows)[i][0].data();
			if (CurrentRxn != NULL) {
				int start = 0;
				int stop = 2;
				if ((*rows)[i][1].compare("reverse") == 0) {
					start = 2;
					stop = 3;
				}
				for (int j=start; j < stop; j++) {
					MFAVariable* newvar = CurrentRxn->GetMFAVar(variables[j]);
					if (newvar != NULL) {
					    double penalty = atof((*rows)[i][2].data());
						if (BaseCoefficients.count(newvar) > 0) {
						  penalty = penalty * BaseCoefficients[newvar];
						}
						penalty = penalty*alphap;
						penaltysum += penalty;
						// wait to scale after summing for max penalty, since max scaling value would be 1
						if (!InParameters->ReactionsUse && InParameters->ScalePenaltyByFlux) {
						  // according to Chris's document, need to check if newvar->Max is zero, in which case scale by maximum flux in any reaction
						  // BUT - it seems like you don't want to accrue more penalty for a reaction that can't carry flux under any circumstance
						  // Do we need to check whether we are really doing gapfilling here? Also have the issue of adding two terms that are
						  // mutually exclusive for any reaction that is reversible
						  if (newvar->Max == 0) {
						    penalty = penalty/maxbound;
						    cout << "Skipping penalty for low expression reaction " << (*rows)[i][0].data() << " because it is blocked." << endl;						    
						    continue; // don't even add this to the objective function for now
						  } else {
						    penalty = penalty/newvar->Max;
						  }
						}
						ObjFunct->Variables.push_back(newvar);
						ObjFunct->Coefficient.push_back(penalty);
					    cout << " pushed USE of type " << variables[j] << " with penalty " << penalty << endl;
					}
				}
			}
		}
	}
	//Removing this re-scaling code because it results in numerically unstable problem formulations
	//for (int i=startsize; i < int(ObjFunct->Variables.size()); i++) {
		//ObjFunct->Coefficient[i] = ObjFunct->Coefficient[i]*omegap/penaltysum;
	//}
	double threshold = 0.5;
	if (!InParameters->ReactionsUse) {
		threshold = MFA_ZERO_TOLERANCE;
	}
	this->SetMin();
	if (InParameters->PhenotypeAnalysis) {
		this->RunDeletionExperiments(InData,InParameters,true);
	} else {
		ofstream output;
		if (OpenOutput(output,(FOutputFilepath()+"GapfillingOutput.txt").data())) {
			output << "Label\tObjective\tGF objective\tRR score/count\tAR score/count\tRJ score/count\tCC score/count\tReactions retained\tActivated reactions\tRejected reactions\tCandidates cut\n";
			output.close();
		}
		bool stay_in_loop = true;
		int currentround = 0;
		int count = 0;
		if (InParameters->GapfillingTargetReactions.size() > 0) {
			for (int i=0; i < int(InParameters->GapfillingTargetReactions.size()); i++) {
				Reaction* current = InData->FindReaction("DATABASE",InParameters->GapfillingTargetReactions[i].data());
				if (current != NULL) {
					MFAVariable* currvar = current->GetMFAVar(REACTION_SLACK);
					if (currvar != NULL) {
						currvar->UpperBound = 0;
						LoadVariable(currvar->Index);
						SolveGapfillingProblem(1,CurrentSolution,InParameters->GapfillingTargetReactions[i],InParameters,"GapfillingOutput.txt");
						currvar->UpperBound = 1;
						LoadVariable(currvar->Index);
					}
				}
			}
		} else {
			while (stay_in_loop) {
				stay_in_loop = SolveGapfillingProblem(currentround,CurrentSolution,"objective",InParameters,"GapfillingOutput.txt");
				//if (InParameters->alpha == 0) {
					//If this is a straight up gapfilling with no activation, then we only solve once
					stay_in_loop = false;
				//}
				currentround++;
			}
		}
		delete ObjFunct;
	}

	if (originalsense) {
		this->SetMax();
	}
	ObjFunct = OldObjective;
	this->RelaxIntegerVariables = false;
	if (this->Solver != CPLEX) {
		this->Solver = SOLVER_SCIP;
	}
	this->LoadSolver();
	this->RunImplementedGapfillingSolution(InData,InParameters,CurrentSolution);
	CurrentOptimum = CurrentSolution->Objective;
	return SUCCESS;
}

int MFAProblem::RunImplementedGapfillingSolution(Data* InData, OptimizationParameter* InParameters,OptSolutionData*& CurrentSolution) {
	cout << "Run biomass maximization one more time with reaction use variables set according to results of activation and inactivation" << endl;
	int state = this->SaveState();
	vector<int> variables;
	variables.push_back(FLUX);
	variables.push_back(FORWARD_FLUX);
	variables.push_back(REVERSE_FLUX);
	if (InParameters->ReactionsUse) {
		variables[0] = REACTION_USE;
		variables[1] = FORWARD_USE;
		variables[2] = REVERSE_USE;
	}

	vector<string>* gapfilledrxns = StringToStrings(GetParameter("current gapfilled reactions"),";"); // low expression, carry flux
	vector<string>* activatedrxns = StringToStrings(GetParameter("current activated reactions"),";"); // high expression, carry flux
	vector<string>* notactivatedrxns = StringToStrings(GetParameter("current rejected reactions"),";"); // high expression, do not carry flux
	vector<string>* cutrxns = StringToStrings(GetParameter("current cut candidate reactions"),";"); // low expression, do not carry flux

	for (int gfri=0; gfri < (*gapfilledrxns).size(); gfri++) {
		string ReactionID = (*gapfilledrxns)[gfri];
		string Sign = ReactionID.substr(0,1);
		ReactionID = ReactionID.substr(1,ReactionID.length()-1);
		Reaction* current = InData->FindReaction("DATABASE",ReactionID.data());
		if (current != NULL) {
			MFAVariable* currvar = current->GetMFAVar(REACTION_SLACK);
			if (currvar != NULL) {
				currvar->UpperBound = 0;
				currvar->LowerBound = 0;
				//cout << "Set reaction variable of type " << currvar->Type << " to zero for " << currvar->AssociatedReaction->GetData("DATABASE",STRING) << endl;
			}
			for (int vi=0; vi < int(variables.size()); vi++) {
				MFAVariable* currvar = current->GetMFAVar(variables[vi]);
				if (currvar != NULL) {
					if ((Sign.compare("+") == 0 && vi == 2) || (Sign.compare("-") == 0 && vi < 2)) {
						currvar->UpperBound = 0;
						currvar->LowerBound = 0;
						//cout << "Set reaction variable of type " << currvar->Type << " to zero for " << currvar->AssociatedReaction->GetData("DATABASE",STRING) << endl;
					}
				}
			}
		}
	}
	//Forcing activated reactions to nonzero flux by setting slack to zero
	LinEquation* newconstraint = InitializeLinEquation("At least forcing SLACK variables to minimal values",1,LESS, LINEAR);
	for (int ari=0; ari < (*activatedrxns).size(); ari++) {
		string ReactionID = (*activatedrxns)[ari];
		//cout << "Checking activated rxn " << ReactionID << endl;
		Reaction* current = InData->FindReaction("DATABASE",ReactionID.data());
		if (current != NULL) {
			MFAVariable* currvar = current->GetMFAVar(REACTION_SLACK);
			if (currvar != NULL) {
				newconstraint->Variables.push_back(currvar);
				newconstraint->Coefficient.push_back(1);
				//currvar->UpperBound = 0;
				//currvar->LowerBound = 0;
				//cout << "Set reaction variable of type " << currvar->Type << " to zero for " << currvar->AssociatedReaction->GetData("DATABASE",STRING) << endl;
			}
		}
	}
	this->AddConstraint(newconstraint);
	//Forcing cut reactions to zero flux either through flux directly or through use variables
	for (int nari=0; nari < notactivatedrxns->size(); nari++) {
		string ReactionID = (*notactivatedrxns)[nari];
		//cout << "Checking not-actived rxn " << ReactionID << endl;
		Reaction* current = InData->FindReaction("DATABASE",ReactionID.data());
		if (current != NULL) {
			for (int vi=0; vi < int(variables.size()); vi++) {
				MFAVariable* currvar = current->GetMFAVar(variables[vi]);
				if (currvar != NULL) {
					currvar->UpperBound = 0;
					currvar->LowerBound = 0;
					//cout << "Set reaction variable of type " << currvar->Type << " to zero for " << currvar->AssociatedReaction->GetData("DATABASE",STRING) << endl;
				}
			}
		}
	}
	//Forcing cut reactions to zero flux either through flux directly or through use variables
	for (int cri=0; cri < (*cutrxns).size(); cri++) {
		//cout << "Checking cut rxn " << (*cutrxns)[cri] << endl;
		string ReactionID = (*cutrxns)[cri].substr(1,(*cutrxns)[cri].length()-1);
		string Sign = (*cutrxns)[cri].substr(0,1);
		Reaction* current = InData->FindReaction("DATABASE",ReactionID.data());
		if (current != NULL) {
			for (int vi=0; vi < int(variables.size()); vi++) {
				MFAVariable* currvar = current->GetMFAVar(variables[vi]);
				if (currvar != NULL) {
					if ((Sign.compare("+") == 0 && vi < 2) || (Sign.compare("-") == 0 && vi == 2)) {
						currvar->UpperBound = 0;
						currvar->LowerBound = 0;
						//cout << "Set reaction variable of type " << currvar->Type << " to zero for " << currvar->AssociatedReaction->GetData("DATABASE",STRING) << endl;
					}
				}
			}
		}
	}
	ObjectiveConstraint->RightHandSide = 0; // we're maximizing this anyway
	this->ResetSolver();
	this->LoadSolver();
	CurrentSolution = RunSolver(true,false,true);
	cout << "biomass flux with expression constraints: " << CurrentSolution->Objective << endl;
	if (CurrentSolution->Status != SUCCESS) {
		SetParameter("expression informed biomass optimization","fail");
		return FAIL;
	}
	//this->LoadState(state,true,true,true,false,true);
	this->ClearState(state);
	return SUCCESS;
}

vector<MFAVariable*> MFAProblem::BiomassSensitivityAnalysis(OptSolutionData*& CurrentSolution,OptimizationParameter* InParameters) {
	if (ObjectiveConstraint == NULL) {
		ObjectiveConstraint = MakeObjectiveConstraint(0.1,GREATER);
	} else {
		ObjectiveConstraint->RightHandSide = 0.1;
	}
	LinEquation* OldObjective = this->ObjFunct;
	bool is_max = this->FMax();
	this->ObjFunct = InitializeLinEquation("biomass supply objective",0);
	vector<MFAVariable*> activated_variables;
	for (int i=0; i < int(this->Variables.size()); i++) {
		if (this->Variables[i]->Exclude) {
			this->Variables[i]->Exclude = false;
			ObjFunct->Variables.push_back(this->Variables[i]);
			ObjFunct->Coefficient.push_back(1);
		}
	}
	this->SetMin();
	this->LoadSolver();
	CurrentSolution = this->RunSolver(false,false,false);
	string biomass_removals = "";
	for (int i=0; i < int(ObjFunct->Variables.size()); i++) {
		if (CurrentSolution->SolutionData[ObjFunct->Variables[i]->Index] <= MFA_ZERO_TOLERANCE) {
			ObjFunct->Variables[i]->Exclude = true;
		} else {
			activated_variables.push_back(ObjFunct->Variables[i]);
		}
	}
	ObjectiveConstraint->RightHandSide = 0;
	this->ObjFunct = OldObjective;
	if (is_max) {
		this->SetMax();
	}
	this->LoadSolver();
	CurrentSolution = this->RunSolver(false,false,false);
	return activated_variables;
}

bool MFAProblem::SolveGapfillingProblem(int currentround,OptSolutionData*& CurrentSolution,string label,OptimizationParameter* InParameters,string OutputFilename) {
	//Saving state, which makes a copy of the current variables, constraints, and objectives
	int state = this->SaveState();
	int vartype;
	int CurrentObjectiveSize = int(ObjFunct->Variables.size());
	bool stay_in_loop = false;
	bool currentuse = InParameters->ReactionsUse;
	ofstream output;
	bool fileopen = OpenOutput(output,(FOutputFilepath()+OutputFilename).data(),true);
	//Iterate over the number of requested solutions
	MFAVariable* NewSolutionForcingVariable = NULL;
	OptSolutionData* InternalSolution;
	vector<int> first_solution_variables;
	bool slacks_in_objective = false;
	for (int i=0; i < this->ObjFunct->Variables.size(); i++) {
		if (this->ObjFunct->Variables[i]->Type == REACTION_SLACK) {
			slacks_in_objective = true;
			InParameters->NumSolutions = 1;
		}
	}
	for (int m=0; m < InParameters->RecursiveMILPSolutionLimit; m++) {
		//Resetting the solver to ensure all bounds and constraints are synchronized with solver
		this->ResetSolver();
		this->LoadSolver(false);
		//Running the default problem
		InternalSolution = RunSolver(true,false,true);
		//If this is the first solution of the first round, print the solution
		if (currentround == 0 && m == 0) {
			PrintSolutions(-1,-1,false);
		}
		if (InternalSolution->Status != SUCCESS) {
			SetParameter("current gapfilled reactions","fail");
			return false;
		}
		//RR:on and rejected,AR:on and activated,RJ:off and activate or activated candidates,CC:candidate reaction cut
		string rxns [4] = { "","","","" };
		int counts [4] = { 0,0,0,0 };
		float scores [4] = { 0,0,0,0 };
		double objective = 0;
		double gfobjective = 0;
		//Now adding a step in case binary variables are not being used, which intoduces binary variables for key terms and reminimizes
		int gfstart = 0;
		if (InParameters->omega > 0) {
			gfstart = int(this->ObjectiveConstraint->Variables.size());
		}
		vector<MFAVariable*> solutionvariables;
		if (InParameters->ReactionsUse == false && slacks_in_objective == false) {
			LinEquation* NewObjFunct = InitializeLinEquation("objective",0,EQUAL);
			for (int i=0; i < this->ObjFunct->Variables.size(); i++) {
				if (i >= gfstart && ObjFunct->Coefficient[i] > 0) {
					MFAVariable* current = this->ObjFunct->Variables[i];
					double value = InternalSolution->SolutionData[ObjFunct->Variables[i]->Index];
					if (value > MFA_ZERO_TOLERANCE) {
						if (current->Type == REACTION_SLACK) {
							ObjFunct->Variables[i]->Binary = true;
							NewObjFunct->Variables.push_back(ObjFunct->Variables[i]);
							NewObjFunct->Coefficient.push_back(ObjFunct->Coefficient[i]);
						} else if (current->Type == FLUX || current->Type == FORWARD_FLUX || current->Type == REVERSE_FLUX) {
							string sign = "+";
							int vartype = REACTION_USE;
							if (current->Type == REVERSE_FLUX || current->Type == REVERSE_USE) {
								sign = "-";
								vartype = REVERSE_USE;
							} else if (current->Type == FORWARD_FLUX) {
								vartype = FORWARD_USE;
							}
							MFAVariable* NewVariable;
							if (current->AssociatedReaction->MFAVariables[vartype] == NULL) {
								NewVariable = InitializeMFAVariable();
								NewVariable->AssociatedReaction = ObjFunct->Variables[i]->AssociatedReaction;
								NewVariable->Name.assign(sign);
								NewVariable->Name.assign(NewVariable->AssociatedReaction->GetData("DATABASE",STRING));
								NewVariable->Type = vartype;
								NewVariable->UpperBound = 1;
								NewVariable->LowerBound = 0;
								NewVariable->Binary = true;
								this->AddVariable(NewVariable);
								current->AssociatedReaction->MFAVariables[vartype] = NewVariable;
								LinEquation* newconstraint = InitializeLinEquation("Use constraints",0,LESS);
								newconstraint->Variables.push_back(current);
								newconstraint->Variables.push_back(NewVariable);
								newconstraint->Coefficient.push_back(1);
								newconstraint->Coefficient.push_back(-1*current->UpperBound);
								this->AddConstraint(newconstraint);
							} else {
								NewVariable = current->AssociatedReaction->MFAVariables[vartype];
							}
							NewObjFunct->Variables.push_back(NewVariable);
							NewObjFunct->Coefficient.push_back(ObjFunct->Coefficient[i]);
						} else if (current->Type == DRAIN_FLUX || current->Type == FORWARD_DRAIN_FLUX || current->Type == REVERSE_DRAIN_FLUX) {
							string sign = "+";
							int vartype = DRAIN_USE;
							if (current->Type == REVERSE_DRAIN_FLUX || current->Type == REVERSE_DRAIN_USE) {
								sign = "-";
								vartype = REVERSE_DRAIN_USE;
							} else if (current->Type == FORWARD_DRAIN_FLUX) {
								vartype = FORWARD_DRAIN_USE;
							}
							MFAVariable* NewVariable;
							if (current->AssociatedSpecies->GetMFAVar(vartype,current->Compartment) == NULL) {
								NewVariable = current->AssociatedSpecies->CreateMFAVariable(vartype,current->Compartment,0, 1);;
								NewVariable->Name.assign(sign);
								NewVariable->Name.assign(NewVariable->AssociatedSpecies->GetData("DATABASE",STRING));
								NewVariable->Binary = true;
								this->AddVariable(NewVariable);
								LinEquation* newconstraint = InitializeLinEquation("Use constraints",0,LESS);
								newconstraint->Variables.push_back(current);
								newconstraint->Variables.push_back(NewVariable);
								newconstraint->Coefficient.push_back(1);
								newconstraint->Coefficient.push_back(-1*current->UpperBound);
								this->AddConstraint(newconstraint);
							} else {
								NewVariable = current->AssociatedSpecies->GetMFAVar(vartype,current->Compartment);
							}
							NewObjFunct->Variables.push_back(NewVariable);
							NewObjFunct->Coefficient.push_back(ObjFunct->Coefficient[i]);
						}
					} else {
						ObjFunct->Variables[i]->UpperBound = 0;
						ObjFunct->Variables[i]->LowerBound = 0;
					}
				}
			}
			//Minimizing modified objective with only boolean variables
			LinEquation* oldobj = ObjFunct;
			ObjFunct = NewObjFunct;
			this->LoadSolver(false);
			InternalSolution = RunSolver(true,false,true);
			for (int j=0; j < int(NewObjFunct->Variables.size());j++) {
				if (NewObjFunct->Variables[j]->Type != REACTION_SLACK) {
					solutionvariables.push_back(NewObjFunct->Variables[j]);
				}
			}
			//Restoring the old objective
			ObjFunct = oldobj;
		} else {
			for (int j=0; j < int(ObjFunct->Variables.size());j++) {
				if (ObjFunct->Variables[j]->Type != REACTION_SLACK) {
					solutionvariables.push_back(ObjFunct->Variables[j]);
				}
			}
		}
		//Adding constraint to eliminate the current solution from being generated again if we haven't found all desired solutions yet
		if (InParameters->NumSolutions > (m+1)) {
			if (NewSolutionForcingVariable == NULL) {
				NewSolutionForcingVariable = InitializeMFAVariable();
				NewSolutionForcingVariable->LowerBound = 1;
				NewSolutionForcingVariable->UpperBound = 10000;
				this->AddVariable(NewSolutionForcingVariable);
				ObjFunct->Variables.push_back(NewSolutionForcingVariable);
				ObjFunct->Coefficient.push_back(1);
			}
			LinEquation* newconstraint = InitializeLinEquation("Use constraints",int(solutionvariables.size()),EQUAL);
			newconstraint->Variables.push_back(NewSolutionForcingVariable);
			newconstraint->Coefficient.push_back(1);
			for (int j=0; j < int(solutionvariables.size());j++) {
				newconstraint->Variables.push_back(solutionvariables[j]);
				newconstraint->Coefficient.push_back(1);
			}
			this->AddConstraint(newconstraint);
		}
		//Adding up old objective solution value
		for (int i=0; i < ObjectiveConstraint->Coefficient.size(); i++) {
			objective += ObjectiveConstraint->Coefficient[i]*InternalSolution->SolutionData[ObjectiveConstraint->Variables[i]->Index];
		}
		gfobjective = 0;
		for (int i=0; i < CurrentObjectiveSize; i++) {
			if (i >= gfstart && ObjFunct->Coefficient[i] > 0) {
				gfobjective += ObjFunct->Coefficient[i]*InternalSolution->SolutionData[ObjFunct->Variables[i]->Index];
				// double check whether there is flux through the reaction
				//cout << "Checking flux variables for " << ObjFunct->Variables[i]->AssociatedReaction->GetData("DATABASE",STRING) << endl;
				if (ObjFunct->Variables[i]->Type == REACTION_SLACK) {
					int thereWasFlux = 0;
					if (ObjFunct->Variables[i]->AssociatedReaction != NULL) {
						MFAVariable* fluxVar = ObjFunct->Variables[i]->AssociatedReaction->GetMFAVar(FLUX);
						if (fluxVar != NULL) {
							if (InternalSolution->SolutionData[fluxVar->Index] >= MFA_ZERO_TOLERANCE) {
								thereWasFlux = 1;
							}
							//cout << " FLUX is " << CurrentSolution->SolutionData[fluxVar->Index] << endl;
						}
						fluxVar = ObjFunct->Variables[i]->AssociatedReaction->GetMFAVar(FORWARD_FLUX);
						if (fluxVar != NULL) {
							if (InternalSolution->SolutionData[fluxVar->Index] >= MFA_ZERO_TOLERANCE) {
								thereWasFlux = 1;
							}
							//cout << " FORWARD_FLUX is " << CurrentSolution->SolutionData[fluxVar->Index] << endl;
						}
						fluxVar = ObjFunct->Variables[i]->AssociatedReaction->GetMFAVar(REVERSE_FLUX);
						if (fluxVar != NULL) {
							if (InternalSolution->SolutionData[fluxVar->Index] >= MFA_ZERO_TOLERANCE) {
								thereWasFlux = 1;
							}
							//cout << " REVERSE_FLUX is " << CurrentSolution->SolutionData[fluxVar->Index] << endl;
						}
					}
					//cout << " checking REACTION_SLACK for " << ObjFunct->Variables[i]->AssociatedReaction->GetData("DATABASE",STRING) << " with value " << CurrentSolution->SolutionData[ObjFunct->Variables[i]->Index] << endl;
					if (InternalSolution->SolutionData[ObjFunct->Variables[i]->Index] > MFA_ZERO_TOLERANCE) {
						// REACTION_SLACK IS 1; but was there really no flux?
						if (thereWasFlux == 1) {
							cout << " weirdness: REACTION_SLACK but flux" << endl;
							if (rxns[1].length() > 0) {
								rxns[1].append(";");
							}
							rxns[1].append(ObjFunct->Variables[i]->AssociatedReaction->GetData("DATABASE",STRING));
							counts[1]++;
							scores[1] += ObjFunct->Coefficient[i]*InternalSolution->SolutionData[ObjFunct->Variables[i]->Index];
						} else {
							if (rxns[2].length() > 0) {
								rxns[2].append(";");
							}
							rxns[2].append(ObjFunct->Variables[i]->AssociatedReaction->GetData("DATABASE",STRING));
							counts[2]++;
							scores[2] += ObjFunct->Coefficient[i]*InternalSolution->SolutionData[ObjFunct->Variables[i]->Index];
						}
					} else {
						// REACTION_SLACK IS 0; but was there really flux?
						if (thereWasFlux == 0) {
							//cout << " weirdness: no REACTION_SLACK but no flux" << endl;
							if (rxns[2].length() > 0) {
								rxns[2].append(";");
							}
							rxns[2].append(ObjFunct->Variables[i]->AssociatedReaction->GetData("DATABASE",STRING));
							counts[2]++;
							scores[2] += ObjFunct->Coefficient[i]*InternalSolution->SolutionData[ObjFunct->Variables[i]->Index];
						} else {
							if (rxns[1].length() > 0) {
								rxns[1].append(";");
							}
							stay_in_loop = true;
							rxns[1].append(ObjFunct->Variables[i]->AssociatedReaction->GetData("DATABASE",STRING));
							counts[1]++;
							scores[1] += ObjFunct->Coefficient[i];
							if (m==0) {
								first_solution_variables.push_back(i);
							}
						}
					}
				} else if (ObjFunct->Variables[i]->AssociatedReaction != NULL) {
					//cout << " checking USE (type " << ObjFunct->Variables[i]->Type << ") for " << ObjFunct->Variables[i]->AssociatedReaction->GetData("DATABASE",STRING) << " with value " << CurrentSolution->SolutionData[ObjFunct->Variables[i]->Index] << endl;
					if (InternalSolution->SolutionData[ObjFunct->Variables[i]->Index] > MFA_ZERO_TOLERANCE) {
						string sign = "+";
						vartype = REACTION_USE;
						if (ObjFunct->Variables[i]->Type == REVERSE_FLUX || ObjFunct->Variables[i]->Type == REVERSE_USE) {
							sign = "-";
							vartype = REVERSE_USE;
						} else if (ObjFunct->Variables[i]->Type == FORWARD_FLUX) {
							vartype = FORWARD_USE;
						}
						stay_in_loop = true;
						if (rxns[0].length() > 0) {
							rxns[0].append(";");
						}
						cout << "Gapfilling: " << ObjFunct->Variables[i]->AssociatedReaction->GetData("DATABASE",STRING) << endl;
						rxns[0].append(sign+ObjFunct->Variables[i]->AssociatedReaction->GetData("DATABASE",STRING));
						counts[0]++;
						scores[0] += ObjFunct->Coefficient[i]*InternalSolution->SolutionData[ObjFunct->Variables[i]->Index];
						if (m==0) {
							first_solution_variables.push_back(i);
						}
						if (GetParameter("fit phenotype data").compare("1") == 0) {
							ObjFunct->Coefficient[i] = 0;
						}
					} else {
						string sign = "+";
						if (ObjFunct->Variables[i]->Type == REVERSE_FLUX || ObjFunct->Variables[i]->Type == REVERSE_USE) {
							sign = "-";
						}
						if (rxns[3].length() > 0) {
							rxns[3].append(";");
						}
						rxns[3].append(sign+ObjFunct->Variables[i]->AssociatedReaction->GetData("DATABASE",STRING));
						counts[3]++;
						scores[3] += ObjFunct->Coefficient[i];
					}
				} else if (ObjFunct->Variables[i]->AssociatedSpecies != NULL) {
					if (InternalSolution->SolutionData[ObjFunct->Variables[i]->Index] > MFA_ZERO_TOLERANCE) {
						string sign = "+";
						vartype = DRAIN_USE;
						if (ObjFunct->Variables[i]->Type == REVERSE_DRAIN_FLUX || ObjFunct->Variables[i]->Type == REVERSE_DRAIN_USE) {
							sign = "-";
							vartype = REVERSE_DRAIN_USE;
						} else if (ObjFunct->Variables[i]->Type == FORWARD_DRAIN_FLUX) {
							vartype = FORWARD_DRAIN_USE;
						}
						stay_in_loop = true;
						if (rxns[0].length() > 0) {
							rxns[0].append(";");
						}
						cout << "Gapfilling: " << ObjFunct->Variables[i]->AssociatedSpecies->GetData("DATABASE",STRING) << endl;
						this->LoadState(state,true,false,false,false,false);
						LinEquation* oldobj = ObjFunct;
						ObjFunct = InitializeLinEquation("objective",0,EQUAL);
						ObjFunct->Variables.push_back(oldobj->Variables[i]);
						ObjFunct->Coefficient.push_back(1);
						this->LoadSolver();
						OptSolutionData* TempSolution = RunSolver(true,false,true);
						if (TempSolution->Objective == 0) {
							if (sign.compare("+") == 0) {
								sign.assign(">");
							} else {
								sign.assign("<");
							}
						}
						//delete ObjFunct;
						//delete TempSolution;
						ObjFunct = oldobj;
						this->LoadObjective();
						rxns[0].append(sign+ObjFunct->Variables[i]->AssociatedSpecies->GetData("DATABASE",STRING));
						counts[0]++;
						scores[0] += ObjFunct->Coefficient[i]*InternalSolution->SolutionData[ObjFunct->Variables[i]->Index];
						if (m==0) {
							first_solution_variables.push_back(i);
						}
						if (GetParameter("fit phenotype data").compare("1") == 0) {
							ObjFunct->Coefficient[i] = 0;
						}
					} else {
						string sign = "+";
						if (ObjFunct->Variables[i]->Type == REVERSE_DRAIN_FLUX || ObjFunct->Variables[i]->Type == REVERSE_DRAIN_USE) {
							sign = "-";
						}
						if (rxns[3].length() > 0) {
							rxns[3].append(";");
						}
						rxns[3].append(sign+ObjFunct->Variables[i]->AssociatedSpecies->GetData("DATABASE",STRING));
						counts[3]++;
						scores[3] += ObjFunct->Coefficient[i];
					}
				}
			}
		}
		this->LoadState(state,true,false,false,false,false);
		InternalSolution->Objective = objective;
		if (fileopen) {
			cout << label << "|" << m << "|" << round << "\t" << objective << "\t" << gfobjective << "\t" << scores[0] << "/" << counts[0] << "\t" << scores[1] << "/" << counts[1] << "\t" << scores[2] << "/" << counts[2] << "\t" << scores[3] << "/" << counts[3] << "\t" << rxns[0] << "\t" << rxns[1] << "\t" << rxns[2] << "\t" << rxns[3] << "\n";
			output << label << "|" << m << "|" << round << "\t" << objective << "\t" << gfobjective << "\t" << scores[0] << "/" << counts[0] << "\t" << scores[1] << "/" << counts[1] << "\t" << scores[2] << "/" << counts[2] << "\t" << scores[3] << "/" << counts[3] << "\t" << rxns[0] << "\t" << rxns[1] << "\t" << rxns[2] << "\t" << rxns[3] << "\n";
		}
		if (m == 0) {
			CurrentSolution = InternalSolution;
			SetParameter("original objective",dtoa(objective));
			if (stay_in_loop) {
				SetParameter("current gapfilled reactions",rxns[0].data());
			} else {
				SetParameter("current gapfilled reactions","none");
			}
			SetParameter("current activated reactions",rxns[1].data());
			SetParameter("current rejected reactions",rxns[2].data());
			SetParameter("current cut candidate reactions",rxns[3].data());
		}
	}
	//for (int i=0; i < int(first_solution_variables.size()); i++) {
		//ObjFunct->Coefficient[first_solution_variables[i]] = 0;
	//}
	output.close();
	this->LoadState(state,true,true,true,false,true);
	this->ClearState(state);
	return stay_in_loop;
}

int MFAProblem::ReactionSensitivityAnalysis(Data* InData,OptSolutionData*& CurrentSolution,OptimizationParameter* InParameters) {
	int state = this->SaveState();
	//this->ObjectiveConstraint->RightHandSide = InParameters->OptimalObjectiveFraction*CurrentSolution->Objective;
	this->ObjectiveConstraint->RightHandSide = 0.1;
	//Setting objective to minimize the sum of slack variables associated with reactions currently carrying flux in the current solution
	this->ObjFunct = InitializeLinEquation("minimize slack variables",0);
	vector<MFAVariable*> biomass_supplies;
	this->SetMin();
	vector<MFAVariable*> off_variables;
	vector<double> original_lower_bounds;
	vector<double> original_upper_bounds;
	for (int i=0; i < int(this->Variables.size()); i++) {
		if (this->Variables[i]->Exclude == false && (this->Variables[i]->Type == FLUX || this->Variables[i]->Type == FORWARD_FLUX || this->Variables[i]->Type == REVERSE_FLUX)) {
			cout << this->Variables[i]->AssociatedReaction->GetData("DATABASE",STRING) << "\t" << CurrentSolution->SolutionData[this->Variables[i]->Index] << endl;
			if (CurrentSolution->SolutionData[this->Variables[i]->Index] > MFA_ZERO_TOLERANCE || CurrentSolution->SolutionData[this->Variables[i]->Index] < -MFA_ZERO_TOLERANCE) {
				cout << "test1;";
				if (!this->Variables[i]->AssociatedReaction->IsBiomassReaction()) {
					cout << "test2;";
					MFAVariable* currentvar = this->Variables[i]->AssociatedReaction->GetMFAVar(REACTION_SLACK);
					if (currentvar != NULL) {
						cout << "test3;";
						this->ObjFunct->Variables.push_back(currentvar);
						this->ObjFunct->Coefficient.push_back(1);
						if (this->Variables[i]->Type == FORWARD_FLUX) {
							cout << "test4;";
							currentvar = this->Variables[i]->AssociatedReaction->GetMFAVar(REVERSE_FLUX);
							if (currentvar != NULL) {
								currentvar->UpperBound = 0;
								currentvar->LowerBound = 0;
							}
						} else if (this->Variables[i]->Type == REVERSE_FLUX) {
							cout << "test5;";
							currentvar = this->Variables[i]->AssociatedReaction->GetMFAVar(FORWARD_FLUX);
							if (currentvar != NULL) {
								currentvar->UpperBound = 0;
								currentvar->LowerBound = 0;
							}
						}
					}
				}
			} else {
				off_variables.push_back(this->Variables[i]);
				original_lower_bounds.push_back(this->Variables[i]->LowerBound);
				original_upper_bounds.push_back(this->Variables[i]->UpperBound);
			}
			cout << endl;
		}
	}
	for (int i=0; i < int(this->Variables.size()); i++) {
		if (this->Variables[i]->Exclude) {
			this->Variables[i]->Exclude = false;
			biomass_supplies.push_back(this->Variables[i]);
			this->ObjFunct->Variables.push_back(this->Variables[i]);
			this->ObjFunct->Coefficient.push_back(100);
		}
	}
	this->RelaxIntegerVariables = true;
	if (this->Solver != CPLEX) {
		this->Solver = GLPK;
	}
	this->LoadSolver();
	//Iterate over reactions
	ofstream output;
	if(!OpenOutput(output,(FOutputFilepath()+"ReactionSensitivity.txt").data(),false)) {
		return FAIL;
	}
	output << "Reaction\tBiomassAdjMin\tCoupledReactionsMin\tBiomassAdj\tCoupledReactions" << endl;
	for (int i=0; i < int(this->ObjFunct->Variables.size()); i++) {
		if (this->ObjFunct->Coefficient[i] == 100) {
			break;
		}
		for (int j=0; j < int(off_variables.size()); j++) {
			off_variables[j]->LowerBound = 0;
			off_variables[j]->UpperBound = 0;
			LoadVariable(off_variables[j]->Index);
		}
		Reaction* currentreaction = this->ObjFunct->Variables[i]->AssociatedReaction;
		cout << "Iteration " << i << " " << currentreaction->GetData("DATABASE",STRING) << endl;
		output << currentreaction->GetData("DATABASE",STRING) << "\t";
		//Checking for essentiality and setting bounds on fluxes to zero
		MFAVariable* currentvar = currentreaction->GetMFAVar(FLUX);
		vector<double> oldlowerbound;
		vector<double> oldupperbound;
		vector<MFAVariable*> fluxvars;
		if (currentvar != NULL) {
			oldupperbound.push_back(currentvar->UpperBound);
			oldlowerbound.push_back(currentvar->LowerBound);
			currentvar->UpperBound = 0;
			currentvar->LowerBound = 0;
			fluxvars.push_back(currentvar);
			LoadVariable(currentvar->Index);
		} else {
			currentvar = currentreaction->GetMFAVar(FORWARD_FLUX);
			if (currentvar != NULL) {
				oldupperbound.push_back(currentvar->UpperBound);
				oldlowerbound.push_back(currentvar->LowerBound);
				currentvar->UpperBound = 0;
				currentvar->LowerBound = 0;
				fluxvars.push_back(currentvar);
				LoadVariable(currentvar->Index);
			}
			currentvar = currentreaction->GetMFAVar(REVERSE_FLUX);
			if (currentvar != NULL) {
				oldupperbound.push_back(currentvar->UpperBound);
				oldlowerbound.push_back(currentvar->LowerBound);
				currentvar->UpperBound = 0;
				currentvar->LowerBound = 0;
				fluxvars.push_back(currentvar);
				LoadVariable(currentvar->Index);
			}
		}
		OptSolutionData* NewSolution = this->RunSolver(false,false,false);
		if (NewSolution->Status != SUCCESS) {
			LoadSolver();
			NewSolution = this->RunSolver(false,false,false);
		}
		string couplereactions("NOSOLUTION");
		string coupledbiomass("NOSOLUTION");
		if (NewSolution->Status == SUCCESS) {
			//Compare current slacks against old slacks and record all new ones
			couplereactions.assign("");
			coupledbiomass.assign("");
			for (int j=0; j < int(this->ObjFunct->Variables.size()); j++) {
				if (i != j && NewSolution->SolutionData[this->ObjFunct->Variables[j]->Index] > MFA_ZERO_TOLERANCE) {
					if (this->ObjFunct->Coefficient[j] == 100) {
						coupledbiomass.append(this->ObjFunct->Variables[j]->AssociatedReaction->GetData("DATABASE",STRING));
						coupledbiomass.append(";");
					} else {
						couplereactions.append(this->ObjFunct->Variables[j]->AssociatedReaction->GetData("DATABASE",STRING));
						couplereactions.append(";");
					}
				}
			}
		}
		output << coupledbiomass << "\t" << couplereactions << "\t";
		for (int j=0; j < int(off_variables.size()); j++) {
			off_variables[j]->LowerBound = original_lower_bounds[j];
			off_variables[j]->UpperBound = original_upper_bounds[j];
			LoadVariable(off_variables[j]->Index);
		}
		NewSolution = this->RunSolver(false,false,false);
		if (NewSolution->Status != SUCCESS) {
			LoadSolver();
			NewSolution = this->RunSolver(false,false,false);
		}
		couplereactions.assign("NOSOLUTION");
		coupledbiomass.assign("NOSOLUTION");
		if (NewSolution->Status == SUCCESS) {
			//Compare current slacks against old slacks and record all new ones
			couplereactions.assign("");
			coupledbiomass.assign("");
			for (int j=0; j < int(this->ObjFunct->Variables.size()); j++) {
				if (i != j && NewSolution->SolutionData[this->ObjFunct->Variables[j]->Index] > MFA_ZERO_TOLERANCE) {
					if (this->ObjFunct->Coefficient[j] == 100) {
						coupledbiomass.append(this->ObjFunct->Variables[j]->AssociatedReaction->GetData("DATABASE",STRING));
						coupledbiomass.append(";");
					} else {
						couplereactions.append(this->ObjFunct->Variables[j]->AssociatedReaction->GetData("DATABASE",STRING));
						couplereactions.append(";");
					}
				}
			}
		}
		output << coupledbiomass << "\t" << couplereactions << endl;
		for (int j=0; j < int(fluxvars.size()); j++) {
			fluxvars[j]->UpperBound = oldupperbound[j];
			fluxvars[j]->LowerBound = oldlowerbound[j];
			LoadVariable(fluxvars[j]->Index);
		}
	}
	output.close();
	this->LoadState(state,true,true,true,false,true);
	this->ClearState(state);
	this->RelaxIntegerVariables = false;
	if (this->Solver != CPLEX) {
		this->Solver = SOLVER_SCIP;
	}
	this->LoadSolver();
	return SUCCESS;
}

int MFAProblem::GapGeneration(Data* InData, OptimizationParameter* InParameters) {
	string Note;
	int NumOriginalVariables = 0;

	//Clearing any existing problem
	if (Variables.size() > 0 || Constraints.size() > 0) {
		ClearObjective();
		ClearConstraints();
		ClearVariables();
	}
	//Loading biomass component reactions, which provide for new components in biomass
	//this->LoadBiomassDrainReactions(InData,InParameters);
	BuildMFAProblem(InData,InParameters);

	//Adding the objective to the original problem
	LinEquation* NewObjective = ConvertStringToObjective(GetParameter("objective"), InData);
	SetMax();

	//Loading the problem data into the solver if it is not already loaded... this automatically resets the solver
	ResetSolver();
	int Status = LoadSolver(true);
	if (Status != SUCCESS) {
		Note.append("Problem failed to load into solver");
		PrintProblemReport(FLAG,InParameters,Note);
		return FAIL;
	}

	//Running the solver and obtaining and checking the solution returned.
	OptSolutionData* NewSolution = RunSolver(true,true,true);
	if (NewSolution == NULL) {
		Note.append("Problem failed to return a solution");
		PrintProblemReport(FLAG,InParameters,Note);
		return FAIL;
	} else if (NewSolution->Status != SUCCESS) {
		Note.append("Returned solution is infeasible");
		PrintProblemReport(FLAG,InParameters,Note);
		return FAIL;
	}
	if (NewSolution->Objective == 0) {
		Note.append("Original objective is zero. Gap generation not necessary.");
		PrintProblemReport(FLAG,InParameters,Note);
		return FAIL;
	}
	double ObjectiveValue = NewSolution->Objective;
	NumOriginalVariables = FNumVariables();

	//Building the dual problem
	MFAProblem* NewProblem = new MFAProblem();
	NewProblem->BuildDualMFAProblem(this,InData,InParameters);
	ResetIndecies();

	//Cloning the original problem
	vector<MFAVariable*> SecondNetworkVariables;
	for (int i=0; i < FNumVariables(); i++) {
		if (!GetVariable(i)->Binary) {
			MFAVariable* NewVariable = CloneVariable(GetVariable(i));
			SecondNetworkVariables.push_back(NewVariable);
		} else {
			SecondNetworkVariables.push_back(GetVariable(i));
		}
	}
	vector<LinEquation*> SecondNetworkEquations;
	for (int i=0; i < FNumConstraints(); i++) {
		LinEquation* NewEquation = CloneLinEquation(GetConstraint(i));
		for (int j=0; j < int(NewEquation->Variables.size()); j++) {
			NewEquation->Variables[j] = SecondNetworkVariables[NewEquation->Variables[j]->Index];
		}
		SecondNetworkEquations.push_back(NewEquation);
	}
	LinEquation* SecondNetworkObjective = CloneLinEquation(GetObjective());
	for (int j=0; j < int(SecondNetworkObjective->Variables.size()); j++) {
		SecondNetworkObjective->Variables[j] = SecondNetworkVariables[SecondNetworkObjective->Variables[j]->Index];
	}
	//Adding second network variables and constraints
	for (int i=0; i < int(SecondNetworkVariables.size()); i++) {
		if (SecondNetworkVariables[i]->Binary == false) {
			AddVariable(SecondNetworkVariables[i]);
		}
	}
	for (int i=0; i < int(SecondNetworkEquations.size()); i++) {
		AddConstraint(SecondNetworkEquations[i]);
	}
	//Adding dual constraints and variables
	for (int i=0; i < NewProblem->FNumConstraints(); i++) {
		AddConstraint(NewProblem->GetConstraint(i));
	}
	for (int i=0; i < NewProblem->FNumVariables(); i++) {
		if (NewProblem->GetVariable(i)->Binary == false) {
			AddVariable(NewProblem->GetVariable(i));
		}
	}

	//Creating a constraint setting the dual objective equal to the primal objective
	LinEquation* NewConstraint = InitializeLinEquation();
	for (int i=0; i < int(GetObjective()->Variables.size()); i++) {
		NewConstraint->Variables.push_back(GetObjective()->Variables[i]);
		NewConstraint->Coefficient.push_back(GetObjective()->Coefficient[i]);
	}
	for (int i=0; i < int(NewProblem->GetObjective()->Variables.size()); i++) {
		NewConstraint->Variables.push_back(NewProblem->GetObjective()->Variables[i]);
		NewConstraint->Coefficient.push_back(-NewProblem->GetObjective()->Coefficient[i]);
	}
	NewConstraint->EqualityType = EQUAL;
	NewConstraint->RightHandSide = 0;
	NewConstraint->ConstraintMeaning = "Setting primal objective equal to dual objective";
	AddConstraint(NewConstraint);

	//Setting the original objective to zero
	MakeObjectiveConstraint(0,EQUAL);

	//Setting the second network objective to a fraction of the optimal value
	SecondNetworkObjective->EqualityType = GREATER;
	SecondNetworkObjective->RightHandSide = ObjectiveValue*InParameters->OptimalObjectiveFraction;
	SecondNetworkObjective->ConstraintMeaning = "Forcing growth in the second metabolic network";
	AddConstraint(SecondNetworkObjective);

	//If reactions are knocked out in the original problem, we unknock them out in the second network
	if (InParameters->KOReactions.size() > 0) {
		for (int i=0; i < int(InParameters->KOReactions.size()); i++) {
			Reaction* KOReaction = InData->FindReaction("DATABASE;NAME;ENTRY",InParameters->KOReactions[i].data());
			if (KOReaction != NULL) {
				double MinFlux = -InParameters->MaxFlux;
				double MaxFlux = InParameters->MaxFlux;
				if (KOReaction->FType() == REVERSE) {
					MaxFlux = 0;
				} else if (KOReaction->FType() == FORWARD) {
					MinFlux = 0;
				}
				MFAVariable* TempVariable = KOReaction->GetMFAVar(FLUX);
				if (TempVariable != NULL) {
					TempVariable = SecondNetworkVariables[TempVariable->Index];
					TempVariable->UpperBound = MaxFlux;
					TempVariable->LowerBound = MinFlux;
				} else {
					TempVariable = KOReaction->GetMFAVar(FORWARD_FLUX);
					if (TempVariable != NULL) {
						TempVariable = SecondNetworkVariables[TempVariable->Index];
						if (MaxFlux > 0) {
							TempVariable->UpperBound = MaxFlux;
							if (MinFlux > 0) {
								TempVariable->LowerBound = MinFlux;
							} else {
								TempVariable->LowerBound = 0;
							}
						} else {
							TempVariable->UpperBound = 0;
							TempVariable->LowerBound = 0;
						}
					}
					TempVariable = KOReaction->GetMFAVar(REVERSE_FLUX);
					if (TempVariable != NULL) {
						TempVariable = SecondNetworkVariables[TempVariable->Index];
						if (MinFlux < 0) {
							TempVariable->UpperBound = -MinFlux;
							if (MaxFlux > 0) {
								TempVariable->LowerBound = 0;
							} else {
								TempVariable->LowerBound = -InParameters->MaxFlux;
							}
						} else {
							TempVariable->UpperBound = 0;
							TempVariable->LowerBound = 0;
						}
					}
				}
			}
		}
	}
	//Implementing KO of gapgen
	vector<string> gapgenRxn;
	string KO = GetParameter("Gap generation KO genes");
	if (KO.length() > 0 && KO.compare("none") != 0) {
		vector<string>* Strings = StringToStrings(KO,";,");
		for (int i=0; i < InData->FNumGenes(); i++) {
			InData->GetGene(i)->SetMark(false);
		}
		for (int i=0; i < int(Strings->size()); i++) {
			Gene* Temp = InData->FindGene("DATABASE",(*Strings)[i].data());
			if (Temp != NULL) {
				Temp->SetMark(true);
			}
		}
		for (int j=0; j < InData->FNumReactions(); j++) {
			if (InData->GetReaction(j)->CheckForKO()) {
				gapgenRxn.push_back(InData->GetReaction(j)->GetData("DATABASE",STRING));
			}
		}
		for (int i=0; i < InData->FNumGenes(); i++) {
			InData->GetGene(i)->SetMark(false);
		}
		delete Strings;
	}
	KO = GetParameter("Gap generation KO reactions");
	if (KO.length() > 0 && KO.compare("none") != 0) {
		vector<string>* Strings = StringToStrings(KO,";,");
		for (int i=0; i < int(Strings->size()); i++) {
			gapgenRxn.push_back((*Strings)[i]);
		}
		delete Strings;
	}
	for (int i=0; i < int(gapgenRxn.size()); i++) {
		for (int j=0; j < int(SecondNetworkVariables.size()); j++) {
			if (SecondNetworkVariables[j]->Type == FLUX || SecondNetworkVariables[j]->Type == FORWARD_FLUX || SecondNetworkVariables[j]->Type == REVERSE_FLUX) {
				if (SecondNetworkVariables[j]->AssociatedReaction != NULL && SecondNetworkVariables[j]->AssociatedReaction->GetData("DATABASE",STRING).compare(gapgenRxn[i]) == 0) {
					SecondNetworkVariables[j]->UpperBound = 0;
					SecondNetworkVariables[j]->LowerBound = 0;
				}
			}
		}
	}
	if (GetParameter("Gap generation media").compare(GetParameter("user bounds filename")) != 0 && GetParameter("Gap generation media").compare("none") != 0) {
		//Undoing the previous media settings
		double maxDrainFlux = InParameters->MaxDrainFlux;
		if (GetParameter("Gap generation media").compare("Complete") == 0) {
			maxDrainFlux = 100;
		}
		for (int i=0; i < NumOriginalVariables; i++) {
			if (GetVariable(i)->Type == DRAIN_FLUX) {
				SecondNetworkVariables[GetVariable(i)->Index]->UpperBound = maxDrainFlux;
				SecondNetworkVariables[GetVariable(i)->Index]->LowerBound = InParameters->MinDrainFlux;
			} else if (GetVariable(i)->Type == FORWARD_DRAIN_FLUX) {
				if (maxDrainFlux > 0) {
					SecondNetworkVariables[GetVariable(i)->Index]->UpperBound = maxDrainFlux;
				} else {
					SecondNetworkVariables[GetVariable(i)->Index]->UpperBound = 0;
				}
				if (InParameters->MinDrainFlux > 0) {
					SecondNetworkVariables[GetVariable(i)->Index]->LowerBound = InParameters->MinDrainFlux;
				} else {
					SecondNetworkVariables[GetVariable(i)->Index]->LowerBound = 0;
				}
			} else if (GetVariable(i)->Type == REVERSE_DRAIN_FLUX) {
				if (maxDrainFlux < 0) {
					SecondNetworkVariables[GetVariable(i)->Index]->LowerBound = -maxDrainFlux;
				} else {
					SecondNetworkVariables[GetVariable(i)->Index]->LowerBound = 0;
				}
				if (InParameters->MinDrainFlux < 0) {
					SecondNetworkVariables[GetVariable(i)->Index]->UpperBound = -InParameters->MinDrainFlux;
				} else {
					SecondNetworkVariables[GetVariable(i)->Index]->UpperBound = 0;
				}
			}
		}
		//Loading gap generation media
		if (GetParameter("Gap generation media").compare("Complete") != 0) {
			FileBounds* NewBounds = ReadBounds(GetParameter("Gap generation media"));
			for (int i=0; i < int(NewBounds->VarName.size()); i++) {
				if (NewBounds->VarType[i] == DRAIN_FLUX) {
					Species* MediaSpecies = InData->FindSpecies("DATABASE;ENTRY;NAME",NewBounds->VarName[i].data());
					if (MediaSpecies != NULL) {
						MFAVariable* TempVariable = MediaSpecies->GetMFAVar(DRAIN_FLUX,GetCompartment(NewBounds->VarCompartment[i].data())->Index);
						if (TempVariable != NULL) {
							TempVariable = SecondNetworkVariables[TempVariable->Index];
							TempVariable->UpperBound = NewBounds->VarMax[i];
							TempVariable->LowerBound = NewBounds->VarMin[i];
						} else {
							TempVariable = MediaSpecies->GetMFAVar(FORWARD_DRAIN_FLUX,GetCompartment(NewBounds->VarCompartment[i].data())->Index);
							if (TempVariable != NULL) {
								TempVariable = SecondNetworkVariables[TempVariable->Index];
								if (NewBounds->VarMax[i] > 0) {
									TempVariable->UpperBound = NewBounds->VarMax[i];
									if (NewBounds->VarMin[i] > 0) {
										TempVariable->LowerBound = NewBounds->VarMin[i];
									} else {
										TempVariable->LowerBound = 0;
									}
								} else {
									TempVariable->UpperBound = 0;
									TempVariable->LowerBound = 0;
								}
							}
							TempVariable = MediaSpecies->GetMFAVar(REVERSE_DRAIN_FLUX,GetCompartment(NewBounds->VarCompartment[i].data())->Index);
							if (TempVariable != NULL) {
								TempVariable = SecondNetworkVariables[TempVariable->Index];
								if (NewBounds->VarMin[i] < 0) {
									TempVariable->UpperBound = -NewBounds->VarMin[i];
									if (NewBounds->VarMax[i] > 0) {
										TempVariable->LowerBound = 0;
									} else {
										TempVariable->LowerBound = -NewBounds->VarMax[i];
									}
								} else {
									TempVariable->UpperBound = 0;
									TempVariable->LowerBound = 0;
								}
							}
						}
					}
				}
			}
		}
	}

	//Building the objective function
	NewObjective = InitializeLinEquation();
	for (int i=0; i < FNumVariables(); i++) {
		if (GetVariable(i)->Binary) {
			double Coefficient = 1;
			if (GetVariable(i)->AssociatedReaction != NULL) {
				if (GetVariable(i)->AssociatedReaction->FType() != REVERSIBLE) {
					Coefficient++;
					if (GetVariable(i)->AssociatedReaction->FNumGeneGroups() > 0) {
						Coefficient++;
					}
				} else if (GetVariable(i)->AssociatedReaction->FEstDeltaG() != FLAG) {
					if (GetVariable(i)->AssociatedReaction->FmMDeltaG(true) < 0 && GetVariable(i)->Type == FORWARD_USE) {
						Coefficient = Coefficient - 0.2*GetVariable(i)->AssociatedReaction->FmMDeltaG(true);
					} else if (GetVariable(i)->AssociatedReaction->FmMDeltaG(true) > 0 && GetVariable(i)->Type == REVERSE_USE) {
						Coefficient = Coefficient + 0.2*GetVariable(i)->AssociatedReaction->FmMDeltaG(true);
					}
				}
			}
			NewObjective->Variables.push_back(GetVariable(i));
			NewObjective->Coefficient.push_back(Coefficient);
		}
	}
	AddObjective(NewObjective);
	SetMax();
	ResetIndecies();
	vector<int> VariableTypes;
	VariableTypes.push_back(OBJECTIVE_TERMS);
	ResetSolver();
	LoadSolver();

	int solutionCount = this->FNumSolutions();
	if (RecursiveMILP(InData,InParameters,VariableTypes,false) <= 0) {
		Note.append("No gap generation solution exists.");
		PrintProblemReport(FLAG,InParameters,Note);
	} else {
		ofstream Output;
		OpenOutput(Output,FOutputFilepath()+"GapGenerationReport.txt",true);
		Output << "Objective\tReactions" << endl;
		for (int i=solutionCount; i < this->FNumSolutions(); i++) {
			OptSolutionData* newSolution = this->GetSolution(i);
			if (newSolution->Notes.compare("Recursive milp solution") == 0) {
				bool First = true;
				Output << newSolution->Objective << "\t";
				for (int j=0; j < int(NewObjective->Variables.size()); j++) {
					if (newSolution->SolutionData[NewObjective->Variables[j]->Index] < 0.5 && NewObjective->Variables[j]->Type != COMPLEX_USE) {
						if (!First) {
							Output << ",";
						}
						Output << NewObjective->Variables[j]->Name;
						First = false;
					}
				}
				Output << endl;
			}
		}
		Output.close();
	}
	return SUCCESS;
}

int MFAProblem::QuantitativeModelOptimization(Data* InData, OptimizationParameter* InParameters) {
	//Determining max bound in min flux FVA
	double maxbound;
	float boundmult = atof(GetParameter("Quantopt fva bound multiplier").data());
	for (int i=0; i < FNumVariables(); i++) {
		if (GetVariable(i)->AssociatedReaction != NULL && (GetVariable(i)->Type == FORWARD_FLUX || GetVariable(i)->Type == REVERSE_FLUX || GetVariable(i)->Type == FLUX)) {
			GetVariable(i)->Max = boundmult*GetVariable(i)->Max;
			if (GetVariable(i)->Max > maxbound) {
				maxbound = GetVariable(i)->Max;
			}
		}
	}
	//Adding biomass adjustment constraint
	LinEquation* NewConstraint = InitializeLinEquation("Biomass adjustment constraint",0,EQUAL);
	MFAVariable* biovar = ObjFunct->Variables[0];
	vector<string>* ComponentReactions = StringToStrings(GetParameter("Biomass component coefficients"),";");
	for (int i=0; i < int(ComponentReactions->size()); i++) {
		vector<string>* ComponentReaction = StringToStrings((*ComponentReactions)[i],":");
		if (ComponentReaction->size() == 2) {
			Reaction* biorxn = InData->FindReaction("DATABASE",(*ComponentReaction)[0].data());
			if (biorxn != NULL) {
				double origmass = atof((*ComponentReaction)[1].data());
				MFAVariable* var = biorxn->GetMFAVar(FORWARD_FLUX);
				if (var != NULL) {
					LinEquation* BoundConstraint = InitializeLinEquation("Biomass adjustment constraint",0,LESS);
					BoundConstraint->Variables.push_back(var);
					BoundConstraint->Variables.push_back(biovar);
					BoundConstraint->Coefficient.push_back(1);
					if ((*ComponentReaction)[0].compare("EnergyBiomass") == 0) {
						BoundConstraint->Coefficient.push_back(-2*origmass);
					} else {
						BoundConstraint->Coefficient.push_back(-1*(0.9-origmass));
						NewConstraint->Variables.push_back(var);
						NewConstraint->Coefficient.push_back(1);
					}
					this->AddConstraint(BoundConstraint);
				}
				var = biorxn->GetMFAVar(REVERSE_FLUX);
				if (var != NULL) {
					LinEquation* BoundConstraint = InitializeLinEquation("Biomass adjustment constraint",0,LESS);
					BoundConstraint->Variables.push_back(var);
					BoundConstraint->Variables.push_back(biovar);
					BoundConstraint->Coefficient.push_back(1);
					if ((*ComponentReaction)[0].compare("EnergyBiomass") == 0) {
						BoundConstraint->Coefficient.push_back(-0.8*origmass);
					} else {
						BoundConstraint->Coefficient.push_back(-1*(origmass-0.05));
						NewConstraint->Variables.push_back(var);
						NewConstraint->Coefficient.push_back(-1);
					}
					this->AddConstraint(BoundConstraint);
				}
			}
		}
		delete ComponentReaction;
	}
	delete ComponentReactions;
	this->AddConstraint(NewConstraint);
	//Decomposing fluxes into multiple bound levels
	for (int i=0; i < InData->FNumReactions(); i++) {
		InData->GetReaction(i)->DecomposeToPiecewiseFluxBounds(atof(GetParameter("Quantopt threshold").data()),atoi(GetParameter("Quantopt minimum variables").data()),this);
	}
	//Seting use variable constraints for biomass fluxes to equality constraints
	for (int i=0; i < FNumConstraints(); i++) {
		if (this->GetConstraint(i)->AssociatedReaction != NULL) {
			if (this->GetConstraint(i)->AssociatedReaction->GetData("FOREIGN",STRING).compare("BiomassComp") == 0 && this->GetConstraint(i)->ConstraintMeaning.compare("Reaction use constraint") == 0) {
				this->GetConstraint(i)->EqualityType = EQUAL;
			} else if (this->GetConstraint(i)->AssociatedReaction->GetData("FOREIGN",STRING).compare("ATPMAINT") == 0 && this->GetConstraint(i)->ConstraintMeaning.compare("Reaction use constraint") == 0) {
				this->GetConstraint(i)->EqualityType = EQUAL;
			} else if (this->GetConstraint(i)->AssociatedReaction->GetData("FOREIGN",STRING).compare("ATPSYNTH") == 0 && this->GetConstraint(i)->ConstraintMeaning.compare("Reaction use constraint") == 0) {
				this->GetConstraint(i)->EqualityType = EQUAL;
			}
		}
	}
	//Decomposing fluxes into multiple bound levels
	for (int i=0; i < InData->FNumSpecies(); i++) {
		InData->GetSpecies(i)->DecomposeToPiecewiseFluxBounds(atof(GetParameter("Quantopt threshold").data()),atoi(GetParameter("Quantopt minimum variables").data()),this);
	}
	//Building full objective function: normal objective - 0.000001 * sum of fluxes
	float minfluxcoef = atof(GetParameter("QuantOpt min flux coefficient").data());
	this->AddSumObjective(FLUX,false,true,-1*minfluxcoef,false);
	this->AddSumObjective(FORWARD_FLUX,false,true,-1*minfluxcoef,false);
	this->AddSumObjective(REVERSE_FLUX,false,true,-1*minfluxcoef,false);
	//Building the dual problem
	MFAProblem* NewProblem = new MFAProblem();
	NewProblem->BuildDualMFAProblem(this,InData,InParameters);
	ResetIndecies();
	//Adding dual constraints and variables
	for (int i=0; i < NewProblem->FNumConstraints(); i++) {
		AddConstraint(NewProblem->GetConstraint(i));
	}
	for (int i=0; i < NewProblem->FNumVariables(); i++) {
		if (NewProblem->GetVariable(i)->Binary == false) {
			AddVariable(NewProblem->GetVariable(i));
		}
	}
	//Creating a constraint setting the dual objective equal to the primal objective
	NewConstraint = InitializeLinEquation();
	for (int i=0; i < int(GetObjective()->Variables.size()); i++) {
		NewConstraint->Variables.push_back(GetObjective()->Variables[i]);
		NewConstraint->Coefficient.push_back(GetObjective()->Coefficient[i]);
	}
	for (int i=0; i < int(NewProblem->GetObjective()->Variables.size()); i++) {
		NewConstraint->Variables.push_back(NewProblem->GetObjective()->Variables[i]);
		NewConstraint->Coefficient.push_back(-NewProblem->GetObjective()->Coefficient[i]);
	}
	NewConstraint->EqualityType = EQUAL;
	NewConstraint->RightHandSide = 0;
	NewConstraint->ConstraintMeaning = "Setting primal objective equal to dual objective";
	AddConstraint(NewConstraint);
	//Adding constraints supplied to quantopt
	FileBounds* NewBounds = ReadBounds("QuantOptMedia");
	int draintypes[3] = {FORWARD_DRAIN_FLUX,REVERSE_DRAIN_FLUX,DRAIN_FLUX};
	int fluxtypes[3] = {FORWARD_FLUX,REVERSE_FLUX,FLUX};
	for (int i=0; i < int(NewBounds->VarName.size()); i++) {
		if (NewBounds->VarType[i] == DRAIN_FLUX) {
			Species* ConstraintSpecies = InData->FindSpecies("DATABASE;ENTRY;NAME",NewBounds->VarName[i].data());
			if (ConstraintSpecies != NULL) {
				for (int i=0; i < 3; i++) {
					MFAVariable* drainvar = ConstraintSpecies->GetMFAVar(draintypes[i]);
					if (drainvar != NULL) {
						LinEquation* NewConstraint = InitializeLinEquation("Specification constraints",NewBounds->VarMin[i],GREATER);
						NewConstraint->Variables.push_back(drainvar);
						NewConstraint->Coefficient.push_back(1);
						AddConstraint(NewConstraint);
						NewConstraint = InitializeLinEquation("Specification constraints",NewBounds->VarMax[i],LESS);
						NewConstraint->Variables.push_back(drainvar);
						NewConstraint->Coefficient.push_back(1);
						AddConstraint(NewConstraint);
					}
				}
			}
		} else if (NewBounds->VarType[i] == FLUX) {
			Species* ConstraintSpecies = InData->FindSpecies("DATABASE;ENTRY;NAME",NewBounds->VarName[i].data());
			if (ConstraintSpecies != NULL) {
				for (int i=0; i < 3; i++) {
					MFAVariable* fluxvar = ConstraintSpecies->GetMFAVar(fluxtypes[i]);
					if (fluxvar != NULL) {
						LinEquation* NewConstraint = InitializeLinEquation("Specification constraints",NewBounds->VarMin[i],GREATER);
						NewConstraint->Variables.push_back(fluxvar);
						NewConstraint->Coefficient.push_back(1);
						AddConstraint(NewConstraint);
						NewConstraint = InitializeLinEquation("Specification constraints",NewBounds->VarMax[i],LESS);
						NewConstraint->Variables.push_back(fluxvar);
						NewConstraint->Coefficient.push_back(1);
						AddConstraint(NewConstraint);
					}
				}
			}
		}
	}
	//Building the objective function
	LinEquation* NewObjective = InitializeLinEquation();
	double reactioncoef = atof(GetParameter("Quantopt reaction objective coefficient").data());
	double draincoef = atof(GetParameter("Quantopt drain objective coefficient").data());
	double biomasscoef = atof(GetParameter("Quantopt biomass objective coefficient").data());
	double atpsynthcoef = atof(GetParameter("Quantopt atpsynth objective coefficient").data());
	double atpmaintcoef = atof(GetParameter("Quantopt atpsmaint objective coefficient").data());
	for (int i=0; i < FNumVariables(); i++) {
		if (GetVariable(i)->Binary) {
			NewObjective->Variables.push_back(GetVariable(i));
			if (GetVariable(i)->AssociatedReaction != NULL) {
				if (GetVariable(i)->AssociatedReaction->GetData("FOREIGN",STRING).compare("BiomassComp") == 0) {
					NewObjective->Coefficient.push_back(biomasscoef);
				} else if (GetVariable(i)->AssociatedReaction->GetData("FOREIGN",STRING).compare("ATPSYNTH") == 0) {
					NewObjective->Coefficient.push_back(atpsynthcoef);
				} else if (GetVariable(i)->AssociatedReaction->GetData("FOREIGN",STRING).compare("ATPMAINT") == 0) {
					NewObjective->Coefficient.push_back(atpmaintcoef);
				} else {
					NewObjective->Coefficient.push_back(reactioncoef);
				}
			} else if (GetVariable(i)->AssociatedSpecies != NULL) {
				NewObjective->Coefficient.push_back(draincoef);
			}
		}
	}
	for (int i=0; i < NewObjective->Variables.size(); i++) {
		cout << NewObjective->Coefficient[i] << "\t" << GetMFAVariableName(NewObjective->Variables[i]) << endl;
	}
	ObjFunct = NULL;
	AddObjective(NewObjective);
	SetMax();
	ResetIndecies();
	LoadSolver(true);
	vector<int> VariableTypes;
	VariableTypes.push_back(OBJECTIVE_TERMS);
	RecursiveMILP(InData,InParameters,VariableTypes,true);
	return SUCCESS;
}

int MFAProblem::SolutionReconciliation(Data* InData, OptimizationParameter* InParameters) {
	//Loading subsystem data from file
	ifstream Input;
	map<string, double, std::less<string> > SubsystemReactions;
	map<string, double, std::less<string> > SubsystemModelReactions;
	map<string, vector<string>, std::less<string> > ReactionSubsystems;
	if (OpenInput(Input,GetParameter("Function mapping filename"))) {
		GetFileLine(Input);
		do {
			vector<string>* Strings  = GetStringsFileline(Input,"\t");
			if (Strings->size() < 3 || (*Strings)[0].length() == 0 || (*Strings)[2].length() == 0) {
				break;
			}
			if ((*Strings)[2].compare("NONE") != 0) {
				if (SubsystemReactions.count((*Strings)[2]) == 0) {
					SubsystemReactions[(*Strings)[2]] = 0;
				}
				SubsystemReactions[(*Strings)[2]]++;
				ReactionSubsystems[(*Strings)[0]].push_back((*Strings)[2]);
				Reaction* SubsystemReaction = InData->FindReaction("DATABASE",(*Strings)[0].data());
				if (SubsystemReaction != NULL) {
					SubsystemModelReactions[(*Strings)[2]]++;
				}
			}
			delete Strings;
		} while(!Input.eof());
		Input.close();
	}
	Input.clear();

	//Loadining scenario data from file
	map<string, double, std::less<string> > ScenarioReactions;
	map<string, double, std::less<string> > ScenarioModelReactions;
	map<string, vector<string>, std::less<string> > ReactionScenarios;
	if (OpenInput(Input,GetParameter("hope scenarios filename"))) {
		GetFileLine(Input);
		do {
			vector<string>* Strings  = GetStringsFileline(Input,"\t");
			if (Strings->size() == 0 || (*Strings)[0].length() == 0) {
				break;
			}
			if (ScenarioReactions.count((*Strings)[0]) == 0) {
				ScenarioReactions[(*Strings)[0]] = 0;
			}
			ScenarioReactions[(*Strings)[0]]++;
			ReactionScenarios[(*Strings)[1]].push_back((*Strings)[0]);
			if (Strings->size() >= 2 && (*Strings)[0].length() > 0 && (*Strings)[1].length() > 0) {
				Reaction* SubsystemReaction = InData->FindReaction("KEGG",(*Strings)[1].data());
				if (SubsystemReaction != NULL) {
					ScenarioModelReactions[(*Strings)[0]]++;
				}
			}
			delete Strings;
		} while(!Input.eof());
		Input.close();
	}
	Input.clear();

	//Checking to make sure a problem has not already been built
	string Note;
	SourceDatabase = InData;
	if (FNumVariables() > 0) {
		Note.append("A problem has already been built.");
		PrintProblemReport(FLAG,InParameters,Note);
		return FAIL;
	}

	//All variables created will be stored in the following vectors by type
	vector<int> OriginalPerformance;
	vector<MFAVariable*> SolutionVariables;
	vector<MFAVariable*> ObservationVariables;
	vector< vector<int> > ErrorMatrix;
	vector<MFAVariable*> ReactionVariables;
	//In order to find new reactions loaded, we store them in a map
	map<string, Reaction*, std::less<string> > LoadedReactions;
	//In order to find reaction variables, we store them in two maps
	map<Reaction*, MFAVariable*, std::less<Reaction*> > ForwardReactionVariables;
	map<Reaction*, MFAVariable*, std::less<Reaction*> > ReverseReactionVariables;
	map<MFAVariable*, double, std::less<MFAVariable*> > ReactionVariableCoefficients;
	int SolutionIndex = 0;

	//Loading the data on the original model performance
	string Filename = GetDatabaseDirectory(true)+GetParameter("Solution data for model optimization")+"-OPEM.txt";
	ifstream InputTwo;
	if (!OpenInput(InputTwo,Filename)) {
		Note.append("Could not open file specifying original model performance.");
		PrintProblemReport(FLAG,InParameters,Note);
		return FAIL;
	}
	GetFileLine(InputTwo);
	vector<string>* Strings = GetStringsFileline(InputTwo,";");
	for (int i=0; i < int(Strings->size()); i++) {
		OriginalPerformance.push_back(atoi((*Strings)[i].data()));
		ObservationVariables.push_back(NULL);
	}
	delete Strings;
	InputTwo.close();
	InputTwo.clear();

	//Loading the solution data
	Filename = (GetDatabaseDirectory(true)+GetParameter("Solution data for model optimization")+"-GFEM.txt");
	if (OpenInput(Input,Filename)) {
		do {
			Strings = GetStringsFileline(Input,";");
			//If the vector is empty, we assume the file is done and we break out of the loop
			if (Strings->size() == 0) {
				delete Strings;
				break;
			}
			//Checking that the reaction string exists
			if (Strings->size() >= 3 && (*Strings)[3].length() > 0) {
				//Parsing the reaction string
				vector<string>* ReactionStringList = StringToStrings((*Strings)[3],",");
				//Creating solution variable
				MFAVariable* SolutionVariable = InitializeMFAVariable();
				SolutionVariable->Binary = true;
				SolutionVariable->Name = (SolutionVariable->Name + "Solution " + itoa(SolutionIndex));
				SolutionVariable->Type = LUMP_USE;
				SolutionVariable->LowerBound = 0;
				SolutionVariable->UpperBound = 1;
				AddVariable(SolutionVariable);
				SolutionVariables.push_back(SolutionVariable);
				//Creating the solution reaction constraint		
				LinEquation* NewConstraint = InitializeLinEquation((SolutionVariable->Name + " reaction constraint").data(),0,GREATER);
				NewConstraint->Coefficient.push_back(-double(ReactionStringList->size()));
				NewConstraint->Variables.push_back(SolutionVariable);
				//Creating and adding reaction use variables to the solution constraint
				for (int i=0; i < int(ReactionStringList->size()); i++) {
					//Parsing out the reaction ID and direction
					string ReactionID = (*ReactionStringList)[i].substr(1,(*ReactionStringList)[i].length()-1);
					string Sign = (*ReactionStringList)[i].substr(0,1);
					//Checking if another solution already involves the reaction meaning its variables may exist
					Reaction* SolutionReaction = LoadedReactions[ReactionID];
					MFAVariable* ReactionVariable = NULL;
					if (SolutionReaction != NULL) {
						//Checking to see if the variable exists for this reaction in the direction that it's used in this solution
						if (Sign.compare("-") == 0) {
							ReactionVariable = ReverseReactionVariables[SolutionReaction];
						} else {
							ReactionVariable = ForwardReactionVariables[SolutionReaction];
						}
					} else {
						//First we search for the reaction in the model
						SolutionReaction = InData->FindReaction("DATABASE",ReactionID.data());
						if (SolutionReaction == NULL) {
							//If the reaction is not in the model, we load it
							SolutionReaction = new Reaction(ReactionID,InData);
							SolutionReaction->SetType(SolutionReaction->CalculateDirectionalityFromThermo());
							SolutionReaction->SetMark(true);
							SolutionReaction->SetData("COEFFICIENT",1,0);
							//Applying the unbalanced penalty
							if (!SolutionReaction->BalanceReaction(false,false)) {
								SolutionReaction->SetData("COEFFICIENT",(SolutionReaction->GetDoubleData("COEFFICIENT",0)+atof(GetParameter("unbalanced penalty").data())),0);
							}
							//Applying the non kegg reaction penalty
							if (SolutionReaction->GetData("KEGG",DATABASE_LINK).length() == 0) {
								SolutionReaction->SetData("COEFFICIENT",(SolutionReaction->GetDoubleData("COEFFICIENT",0)+atof(GetParameter("non KEGG reaction penalty").data())),0);
							}
							//Applying the transporter penalty
							bool Transport = false;
							bool Stereo = false;
							SolutionReaction->CheckForTransportOrStereo(Transport,Stereo);
							if (Transport) {
								SolutionReaction->SetData("COEFFICIENT",(SolutionReaction->GetDoubleData("COEFFICIENT",0)+atof(GetParameter("transporter penalty").data())),0);
							}
							//Applying the missing structure penalty
							if (SolutionReaction->ContainsUnknownStructures() > 0) {
								SolutionReaction->SetData("COEFFICIENT",(SolutionReaction->GetDoubleData("COEFFICIENT",0)+atof(GetParameter("unknown structure penalty").data())),0);
							}
							//Applying no delta G penalty
							if (SolutionReaction->FEstDeltaG() == FLAG) {
								SolutionReaction->SetData("COEFFICIENT",(SolutionReaction->GetDoubleData("COEFFICIENT",0)+atof(GetParameter("no delta G penalty").data())),0);
							}
							//Applying the subsystem penalty and bonus
							if (ReactionSubsystems[SolutionReaction->GetData("DATABASE",STRING)].size() == 0) {
								SolutionReaction->SetData("COEFFICIENT",(SolutionReaction->GetDoubleData("COEFFICIENT",0)+atof(GetParameter("no subsystem penalty").data())),0);
							} else {
								//Applying the subsystem coverage bonus
								double BestCoverage = 0;
								vector<string> Subsystems = ReactionSubsystems[SolutionReaction->GetData("DATABASE",STRING)];
								for (int i=0;i < int(Subsystems.size()); i++) {
									double CurrentCoverage = SubsystemModelReactions[Subsystems[i]]/SubsystemReactions[Subsystems[i]];
									if (CurrentCoverage > BestCoverage && CurrentCoverage <= 1) {
										BestCoverage = CurrentCoverage;
									}
								}
								SolutionReaction->SetData("COEFFICIENT",(SolutionReaction->GetDoubleData("COEFFICIENT",0)-BestCoverage*atof(GetParameter("subsystem coverage bonus").data())),0);
							}
							//Applying the scenario coverage bonus
							if (ReactionScenarios[SolutionReaction->GetData("KEGG",DATABASE_LINK)].size() > 0) {
								double BestCoverage = 0;
								vector<string> Subsystems = ReactionScenarios[SolutionReaction->GetData("KEGG",DATABASE_LINK)];
								for (int i=0;i < int(Subsystems.size()); i++) {
									double CurrentCoverage = ScenarioModelReactions[Subsystems[i]]/ScenarioReactions[Subsystems[i]];
									if (CurrentCoverage > BestCoverage && CurrentCoverage <= 1) {
										BestCoverage = CurrentCoverage;
									}
								}
								SolutionReaction->SetData("COEFFICIENT",(SolutionReaction->GetDoubleData("COEFFICIENT",0)-BestCoverage*atof(GetParameter("scenario coverage bonus").data())),0);
							}
							//Makeing sure the coefficient has a minimum value
							if (SolutionReaction->GetDoubleData("COEFFICIENT",0) < 0.5) {
								SolutionReaction->SetData("COEFFICIENT",0.5,0);
							}
						}
						SolutionReaction->SetData("THERMODYNAMIC PENALTY",atof(GetParameter("directionality penalty").data()),0);
						if (InData->GetReaction(i)->FEstDeltaG() != FLAG) {
							SolutionReaction->SetData("THERMODYNAMIC PENALTY",(SolutionReaction->GetDoubleData("THERMODYNAMIC PENALTY",0)+fabs(SolutionReaction->FEstDeltaG())/10),0);
						}
						//Adding the reaction to the reaction ID map
						LoadedReactions[ReactionID] = SolutionReaction;
					}
					//If the variable still equals NULL, a new variable is created and added to the map and vector
					if (ReactionVariable == NULL) {
						ReactionVariable = InitializeMFAVariable();
						ReactionVariable->Binary = true;
						ReactionVariable->AssociatedReaction = SolutionReaction;
						ReactionVariable->LowerBound = 0;
						ReactionVariable->UpperBound = 1;
						if (Sign.compare("-") == 0) {
							ReactionVariable->Name = ("-" + SolutionReaction->GetData("DATABASE",STRING));
							ReactionVariable->Type = REVERSE_USE;
							ReverseReactionVariables[SolutionReaction] = ReactionVariable;
							if (SolutionReaction->FType() == REVERSE || SolutionReaction->FType() == REVERSIBLE) {
								//This is a foreign reaction added in the correct direction
								ReactionVariableCoefficients[ReactionVariable] = SolutionReaction->GetDoubleData("COEFFICIENT");
							} else if (SolutionReaction->FMark()) {							
								//This is a foreign reaction added in the incorrect direction
								ReactionVariableCoefficients[ReactionVariable] = SolutionReaction->GetDoubleData("COEFFICIENT") + SolutionReaction->GetDoubleData("THERMODYNAMIC PENALTY");
							} else {
								//This is an irreversible model reaction being made reversible
								ReactionVariableCoefficients[ReactionVariable] = SolutionReaction->GetDoubleData("THERMODYNAMIC PENALTY");
							}
						} else {
							ReactionVariable->Name = ("+" + SolutionReaction->GetData("DATABASE",STRING));
							ReactionVariable->Type = FORWARD_USE;
							ForwardReactionVariables[SolutionReaction] = ReactionVariable;
							if (SolutionReaction->FType() == FORWARD || SolutionReaction->FType() == REVERSIBLE) {
								//This is a foreign reaction added in the correct direction
								ReactionVariableCoefficients[ReactionVariable] = SolutionReaction->GetDoubleData("COEFFICIENT");
							} else if (SolutionReaction->FMark()) {
								//This is a foreign reaction added in the incorrect direction
								ReactionVariableCoefficients[ReactionVariable] = SolutionReaction->GetDoubleData("COEFFICIENT") + SolutionReaction->GetDoubleData("THERMODYNAMIC PENALTY");
							} else {
								//This is an irreversible model reaction being made reversible
								ReactionVariableCoefficients[ReactionVariable] = SolutionReaction->GetDoubleData("THERMODYNAMIC PENALTY");
							}
						}
						ReactionVariables.push_back(ReactionVariable);
						AddVariable(ReactionVariable);
					}
					NewConstraint->Coefficient.push_back(1);
					NewConstraint->Variables.push_back(ReactionVariable);	
				}
				delete ReactionStringList;
				SolutionIndex++;
				AddConstraint(NewConstraint);
			}
			//Adding the error matrix row for this solution
			vector<int> NewRow;
			for (int i=5; i < int(Strings->size()); i++) {
				NewRow.push_back(atoi((*Strings)[i].data()));
			}
			ErrorMatrix.push_back(NewRow);
			delete Strings;
		} while(!Input.eof());

		//Creating the solution error constraints
		for (int i=0; i < int(ErrorMatrix[0].size()); i++) {
			//Creating observation variable
			if (OriginalPerformance[i] == 1 || OriginalPerformance[i] == 3) {
				MFAVariable* ObservationVariable = InitializeMFAVariable();
				ObservationVariable->Binary = true;
				ObservationVariable->LowerBound = 0;
				ObservationVariable->UpperBound = 1;
				ObservationVariable->Name = (ObservationVariable->Name + "Observation " + itoa(i));
				ObservationVariable->Type = COMPLEX_USE;
				AddVariable(ObservationVariable);
				ObservationVariables[i] = ObservationVariable;
				LinEquation* NewConstraint = InitializeLinEquation((ObservationVariables[i]->Name + " error constraint").data(),0,GREATER);
				NewConstraint->Variables.push_back(ObservationVariables[i]);
				if (OriginalPerformance[i] == 1) {
					NewConstraint->Coefficient.push_back(int(ErrorMatrix.size()));
				} else {
					NewConstraint->Coefficient.push_back(1);
					NewConstraint->RightHandSide = 1;
				}
				for (int j=0; j < int(ErrorMatrix.size()); j++) {
					if (ErrorMatrix[j][i] == 2) {
						NewConstraint->Coefficient.push_back(-1);
						NewConstraint->Variables.push_back(SolutionVariables[j]);
					} else if (ErrorMatrix[j][i] == 0) {
						NewConstraint->Coefficient.push_back(1);
						NewConstraint->Variables.push_back(SolutionVariables[j]);
					}
				}
				AddConstraint(NewConstraint);
			}
		}
	}
	Input.close();
	Input.clear();

	//Loading the gap generation solution data
	Filename = (GetDatabaseDirectory(true)+GetParameter("Solution data for model optimization")+"-GGEM.txt");
	if (OpenInput(Input,Filename)) {
		//Resetting the error matrix to hold the data for the gap generation solutions
		ErrorMatrix.clear();
		vector<MFAVariable*> GapGenerationSolutionVariables;
		do {
			Strings = GetStringsFileline(Input,";");
			//If the vector is empty, we assume the file is done and we break out of the loop
			if (Strings->size() == 0) {
				delete Strings;
				break;
			}
			//Checking that the reaction string exists
			if (Strings->size() >= 3 && (*Strings)[3].length() > 0) {
				//Parsing the reaction string
				vector<string>* ReactionStringList = StringToStrings((*Strings)[3],",");
				//Creating solution variable
				MFAVariable* SolutionVariable = InitializeMFAVariable();
				SolutionVariable->Binary = true;
				SolutionVariable->Name = (SolutionVariable->Name + "Solution " + itoa(SolutionIndex));
				SolutionVariable->Type = LUMP_USE;
				SolutionVariable->LowerBound = 0;
				SolutionVariable->UpperBound = 1;
				AddVariable(SolutionVariable);
				SolutionVariables.push_back(SolutionVariable);
				GapGenerationSolutionVariables.push_back(SolutionVariable);
				//Creating the solution reaction constraint		
				LinEquation* NewConstraint = InitializeLinEquation((SolutionVariable->Name + " reaction constraint").data(),0,GREATER);
				NewConstraint->Coefficient.push_back(-double(ReactionStringList->size()));
				NewConstraint->Variables.push_back(SolutionVariable);
				//Creating and adding reaction use variables to the solution constraint
				for (int i=0; i < int(ReactionStringList->size()); i++) {
					//Parsing out the reaction ID and direction
					string ReactionID = (*ReactionStringList)[i].substr(1,(*ReactionStringList)[i].length()-1);
					string Sign = (*ReactionStringList)[i].substr(0,1);
					//Checking if another solution already involves the reaction meaning its variables may exist
					Reaction* SolutionReaction = LoadedReactions[ReactionID];
					MFAVariable* ReactionVariable = NULL;
					if (SolutionReaction != NULL) {
						//Checking to see if the variable exists for this reaction in the direction that it's used in this solution
						if (Sign.compare("-") == 0) {
							ReactionVariable = ReverseReactionVariables[SolutionReaction];
						} else {
							ReactionVariable = ForwardReactionVariables[SolutionReaction];
						}
					} else {
						//First we search for the reaction in the model
						SolutionReaction = InData->FindReaction("DATABASE",ReactionID.data());
						if (SolutionReaction != NULL) {
							//Adding the reaction to the reaction ID map
							LoadedReactions[ReactionID] = SolutionReaction;
						}
					}
					//If the variable still equals NULL, a new variable is created and added to the map and vector
					if (ReactionVariable == NULL) {
						ReactionVariable = InitializeMFAVariable();
						ReactionVariable->Binary = true;
						ReactionVariable->AssociatedReaction = SolutionReaction;
						ReactionVariable->LowerBound = 0;
						ReactionVariable->UpperBound = 1;
						double Coefficient = 1;
						//Penalizing removal of irreversible reactions
						if (SolutionReaction->FType() != REVERSIBLE) {
							Coefficient++;
							//Adding an extra penalty for removal of irreversible reactions with genes
							if (SolutionReaction->FNumGeneGroups() > 0) {
								Coefficient++;
							}
						}
						if (Sign.compare("-") == 0) {
							ReactionVariable->Name = ("-" + SolutionReaction->GetData("DATABASE",STRING));
							ReactionVariable->Type = REVERSE_USE;
							ReverseReactionVariables[SolutionReaction] = ReactionVariable;
							//Penalizing removal of reactions in the direction of favorability
							if (SolutionReaction->FEstDeltaG() != FLAG && SolutionReaction->FmMDeltaG(true) > 0) {
								Coefficient = Coefficient + 0.2*SolutionReaction->FmMDeltaG(true);
							}
						} else {
							ReactionVariable->Name = ("+" + SolutionReaction->GetData("DATABASE",STRING));
							ReactionVariable->Type = FORWARD_USE;
							ForwardReactionVariables[SolutionReaction] = ReactionVariable;
							//Penalizing removal of reactions in the direction of favorability
							if (SolutionReaction->FEstDeltaG() != FLAG && SolutionReaction->FmMDeltaG(true) < 0) {
								Coefficient = Coefficient - 0.2*SolutionReaction->FmMDeltaG(true);
							}
						}
						ReactionVariableCoefficients[ReactionVariable] = Coefficient;
						ReactionVariables.push_back(ReactionVariable);
						AddVariable(ReactionVariable);
					}
					NewConstraint->Coefficient.push_back(1);
					NewConstraint->Variables.push_back(ReactionVariable);	
				}
				delete ReactionStringList;
				SolutionIndex++;
				AddConstraint(NewConstraint);
			}
			//Adding the error matrix row for this solution
			vector<int> NewRow;
			for (int i=5; i < int(Strings->size()); i++) {
				NewRow.push_back(atoi((*Strings)[i].data()));
			}
			ErrorMatrix.push_back(NewRow);
			delete Strings;
		} while(!Input.eof());
		Input.close();
		Input.clear();

		//Creating the false positive solution error constraints
		for (int i=0; i < int(ErrorMatrix[0].size()); i++) {
			if (OriginalPerformance[i] == 0 || OriginalPerformance[i] == 2) {
				MFAVariable* ObservationVariable = InitializeMFAVariable();
				ObservationVariable->Binary = true;
				ObservationVariable->LowerBound = 0;
				ObservationVariable->UpperBound = 1;
				ObservationVariable->Name = (ObservationVariable->Name + "Observation " + itoa(i));
				ObservationVariable->Type = COMPLEX_USE;
				AddVariable(ObservationVariable);
				ObservationVariables[i] = ObservationVariable;
				LinEquation* NewConstraint = InitializeLinEquation((ObservationVariables[i]->Name + " error constraint").data(),0,GREATER);
				NewConstraint->Variables.push_back(ObservationVariables[i]);
				if (OriginalPerformance[i] == 0) {
					NewConstraint->Coefficient.push_back(int(ErrorMatrix.size()));
				} else {
					NewConstraint->Coefficient.push_back(1);
					NewConstraint->RightHandSide = 1;
				}
				for (int j=0; j < int(ErrorMatrix.size()); j++) {
					if (ErrorMatrix[j][i] == 3) {
						NewConstraint->Coefficient.push_back(-1);
						NewConstraint->Variables.push_back(GapGenerationSolutionVariables[j]);
					} else if (ErrorMatrix[j][i] == 1) {
						NewConstraint->Coefficient.push_back(1);
						NewConstraint->Variables.push_back(GapGenerationSolutionVariables[j]);
					}
				}
				AddConstraint(NewConstraint);
			}
		}
	}

	//Creating the objective as maximizing the model fit to data and minimizing the modifications to the model
	LinEquation* NewObjective = InitializeLinEquation();
	for (int i=0; i < int(ObservationVariables.size()); i++) {
		if (ObservationVariables[i] != NULL) {
			NewObjective->Coefficient.push_back(30);
			NewObjective->Variables.push_back(ObservationVariables[i]);
		}
	}
	for (int i=0; i < int(ReactionVariables.size()); i++) {
		NewObjective->Coefficient.push_back(ReactionVariableCoefficients[ReactionVariables[i]]);
		NewObjective->Variables.push_back(ReactionVariables[i]);
	}
	AddObjective(NewObjective);
	SetMin();

	//Loading the problem into the solver
	int Status = LoadSolver(false);
	if (Status != SUCCESS) {
		Note.append("Problem failed to load into solver");
		PrintProblemReport(FLAG,InParameters,Note);
		return FAIL;
	}

	//Knocking out undesirable reactions
	for (int i=0; i < FNumVariables(); i++) {
		if (GetVariable(i)->Type == REVERSE_USE || GetVariable(i)->Type == FORWARD_USE) {
			for (int j=0; j < int(InParameters->KOReactions.size()); j++) {
				if (GetVariable(i)->Name.compare(InParameters->KOReactions[j]) == 0) {
					GetVariable(i)->UpperBound = 0;
					LoadVariable(GetVariable(i)->Index);
				}
			}
		}
	}
	
	//First we sample the solutions with different errors being fixed
	vector<int> VariableTypes;
	VariableTypes.push_back(OBJECTIVE_TERMS);
	VariableTypes.push_back(COMPLEX_USE);
	int NumSolutions = FNumSolutions();
	int TotalSolution = RecursiveMILP(InData,InParameters,VariableTypes,false);	
	
	//Now we sample alternative reaction sets to fix the different errors
	VariableTypes[1] = REACTION_USE;
	VariableTypes.push_back(FORWARD_USE);
	VariableTypes.push_back(REVERSE_USE);
	InParameters->AlternativeSolutionAlgorithm = true;
	for (int i = NumSolutions; i < FNumSolutions(); i++) {
		//Fixing the obersvation variables at their value in this solution
		for (int j=0; j < FNumVariables(); j++) {
			if (GetVariable(j)->Type == COMPLEX_USE) {
				GetVariable(j)->UpperBound = 1;
				GetVariable(j)->LowerBound = 0;
				if (GetSolution(i)->SolutionData[j] > 0.5) {
					GetVariable(j)->LowerBound = 1;
				} else {
					GetVariable(j)->UpperBound = 0;
				}
				LoadVariable(j);
			}
		}
		//Running another recursive MILP to identify alternative reaction sets... this is the only data that's printed
		int TotalSolution = RecursiveMILP(InData,InParameters,VariableTypes,true);
	}
	InParameters->AlternativeSolutionAlgorithm = false;

	//Printing the problem report
	PrintProblemReport(TotalSolution,InParameters,Note);
	
	return SUCCESS;
}

string MFAProblem::MediaSensitivityExperiment(Data* InData, OptimizationParameter* InParameters, vector<MFAVariable*> CurrentKO, vector<MFAVariable*> NonessentialMedia) {
	string Note;

	//Building the problem from the model if it has not already been built
	if (FNumVariables() == 0) {
		if (BuildMFAProblem(InData,InParameters) != SUCCESS) {
			FErrorFile() << "Failed to build optimization problem." << endl;
			FlushErrorFile();
			PrintProblemReport(FLAG,InParameters,"Failed to build optimization problem");
			return "";	
		}
	}

	//Loading the problem
	if (!FProblemLoaded()) {
		if (LoadSolver(true) != SUCCESS) {
			PrintProblemReport(FLAG,InParameters,"Problem failed to load into solver");
			return "";
		}
	}

	//If no list of candidates was supplied, every component in the media is a candidate
	if (NonessentialMedia.size() == 0) {
		for (int i=0; i < FNumVariables(); i++) {
			if ((GetVariable(i)->Type == DRAIN_FLUX || GetVariable(i)->Type == FORWARD_DRAIN_FLUX) && GetVariable(i)->UpperBound > MFA_ZERO_TOLERANCE) {
				bool Found = false;
				for (int j=0; j < int(InParameters->UnremovableMedia.size()); j++) {
					if (InParameters->UnremovableMedia[j].compare(GetVariable(i)->Name) == 0) {
						Found = true;
						break;
					}
				}
				if (!Found) {
					NonessentialMedia.push_back(GetVariable(i));
				}
			}
		}
	}

	//Getting the list of drain fluxes with nonzero upperbounds, which are candidates to remove
	vector<MFAVariable*> NewNonessentialMedia;
	for (int i=0; i < int(NonessentialMedia.size()); i++) {
		//Removing compound from the media by setting the upper bound to 0
		double OldUpperBound = NonessentialMedia[i]->UpperBound;
		NonessentialMedia[i]->UpperBound = 0;
		LoadVariable(NonessentialMedia[i]->Index);
		//Running the problem
		OptSolutionData* Solution = RunSolver(false,false,false);
		//Checking if growth was restored
		if(Solution->Status == SUCCESS && Solution->Objective > MFA_ZERO_TOLERANCE) {
			NewNonessentialMedia.push_back(NonessentialMedia[i]);
		} else {
			if (Note.length() > 0) {
				Note.append("|");
			}
			for (int j=0; j < int(CurrentKO.size()); j++) {
				Note.append(CurrentKO[j]->Name+"+");
			}
			Note.append(NonessentialMedia[i]->Name);
		}
		NonessentialMedia[i]->UpperBound = OldUpperBound;
		LoadVariable(NonessentialMedia[i]->Index);
	}

	if (int(CurrentKO.size()) < atoi(GetParameter("maximum media knockouts").data())) {
		while(NewNonessentialMedia.size() > 0) {
			MFAVariable* CurrentVariable = NewNonessentialMedia[NewNonessentialMedia.size()-1];
			NewNonessentialMedia.pop_back();
			CurrentKO.push_back(CurrentVariable);
			double OldUpperBound = CurrentVariable->UpperBound;
			CurrentVariable->UpperBound = 0;
			LoadVariable(CurrentVariable->Index);
			string Result = MediaSensitivityExperiment(InData,InParameters,CurrentKO,NewNonessentialMedia);
			if (Result.length() > 0 && Note.length() > 0) {
				Note.append("|");
			}
			Note.append(Result);
			CurrentVariable->UpperBound = OldUpperBound;
			LoadVariable(CurrentVariable->Index);
			CurrentKO.pop_back();
		}
	}

	return Note;
}//END: MediaSensitivityExperiment(Data* InData, OptimizationParameter* InParameters, bool AddMedia)

/*FitGimme
Author: Shinnosuke Kondo, Hope College, 6/27/2012. The code is originated from FitMicroarrayAssertions.
Description: Reads in a file with reaction gene expression inconsistency score made based on microarray data. 
	Attempts to run the model in such a way that agreement with multiplication of each inconsistency score and flux is minimized*/
int MFAProblem::FitGIMME(Data* InData) {
	//Read in optimization parameters
	OptimizationParameter* Parameters = ReadParameters();
	//Adjusting settings for study
	Parameters->DecomposeReversible = true;
	Parameters->GeneConstraints = true;
	Parameters->ReactionsUse = true;
	Parameters->AllReactionsUse = true;
	//Building problem
	if (BuildMFAProblem(InData,Parameters) != SUCCESS) {
		cerr << "Could not build gimme problem!" << endl;
		return FAIL;
	}
	//Checking that model grows
	string Note;
	double ObjectiveValue = 0;
	if (OptimizeSingleObjective(InData,Parameters,GetParameter("objective"),false,false,ObjectiveValue,Note) != SUCCESS) {
		cerr << "Could not run gimme problem!" << endl;
		return FAIL;
	}
	if (ObjectiveValue == 0) {
		cerr << "Model did not grow!" << endl;
		return FAIL;
	}
	//Fixing growth to a nonzero value
	if (Max) {
		LoadConstToSolver(MakeObjectiveConstraint(Parameters->OptimalObjectiveFraction*ObjectiveValue,GREATER)->Index);
	} else {
		LoadConstToSolver(MakeObjectiveConstraint(Parameters->OptimalObjectiveFraction*ObjectiveValue,LESS)->Index);
	}
	//Parsing microarray assertion into objective
	vector<string>* RxnCoef = StringToStrings(GetParameter("Gene Inactivity Moderated by Metabolism and Expression"),";");
	if (RxnCoef->size() <= 2) {
		cerr << "No reaction inconsistency value was found!" << endl;
		return FAIL;
	}
	string Model = (*RxnCoef)[0];
	string Index = (*RxnCoef)[1];
	map<string,double> CoeffMap;
	LinEquation* NewObjective = InitializeLinEquation("Gimme objective");
	for (int i=2; i < int(RxnCoef->size()); i++) {
		vector<string>* CoefPair = StringToStrings((*RxnCoef)[i],":");
		Reaction* CurrentReaction = InData->FindReaction("NAME;DATABASE;ENTRY",(*CoefPair)[0].data());
		if (CurrentReaction != NULL) {
			CoeffMap[(*CoefPair)[0]] = atof((*CoefPair)[1].data());
		}
		delete CoefPair;
	} //Shin: Since these variables are inherited from single(biomass) optimization, they already have some constraints(e.g. biomass > 0.1 *opt_biomass).
	for (int i=0; i < FNumVariables(); i++) {
		if (GetVariable(i)->Type == FLUX || GetVariable(i)->Type == FORWARD_FLUX || GetVariable(i)->Type == REVERSE_FLUX) {
			NewObjective->Variables.push_back(GetVariable(i));
			if (CoeffMap.count(GetVariable(i)->Name) > 0) {
				NewObjective->Coefficient.push_back(CoeffMap[GetVariable(i)->Name]);
			} else {
				NewObjective->Coefficient.push_back(0);
			}
		}
	}
	AddObjective(NewObjective);
	SetMin();
	LoadObjective();
	//Optimizing objective
	OptSolutionData* Solution = RunSolver(true,true,true);
	if (Solution == NULL) {
		cerr << "No solution found!" << endl;
		return FAIL;
	}
	//Printing violating reactions in the problem report
	string reactions;

	Note.assign("Violating reactions:");	
	if (Note.length() == 16) {
		Note.assign("No violations");
	} else {
		Note = Note.substr(0,Note.length()-1);
	}
	PrintProblemReport(Solution->Objective,Parameters,Note);
	delete RxnCoef;

	return SUCCESS;
}

int MFAProblem::SoftConstraint(Data* InData) {
	//Read in optimization parameters
	OptimizationParameter* Parameters = ReadParameters();
	//Adjusting settings for study
	Parameters->DecomposeReversible = false;
	Parameters->ReactionsUse = true;
	Parameters->AllReactionsUse = true;
	//Building problem
	if (BuildMFAProblem(InData,Parameters) != SUCCESS) {
		cerr << "Could not build soft constraint problem!" << endl;
		return FAIL;
	}
	//Checking that model grows
	string Note;
	double ObjectiveValue = 0;
	if (OptimizeSingleObjective(InData,Parameters,GetParameter("objective"),false,false,ObjectiveValue,Note) != SUCCESS) {
		cerr << "Could not run soft constraint problem!" << endl;
		return FAIL;
	}
	if (ObjectiveValue == 0) {
		cerr << "Model did not grow!" << endl;
		return FAIL;
	}

	MFAVariable* alpha = InitializeMFAVariable();
	alpha->Name = (alpha->Name + "alpha");
	alpha->Type = REACTION_CONSTRAINT;
	alpha->LowerBound = 0;
	LoadVariable(AddVariable(alpha));

	MFAVariable* beta = InitializeMFAVariable();
	beta->Name = (beta->Name + "beta");
	beta->Type = REACTION_CONSTRAINT;
	beta->LowerBound = 0;
	LoadVariable(AddVariable(beta));

	//Parsing reaction bounds into constraints
	vector<string>* RxnBound = StringToStrings(GetParameter("Soft Constraint"),";");
	if (RxnBound->size() < 1) {
		cerr << "Soft Constraint parameter not specified correctly!" << endl;
		return FAIL;
	}
	double Kappa = atof((*RxnBound)[0].data());
	map<string,map<string,double> > BoundMap;
	for (int i=1; i < int(RxnBound->size()); i++) {
		vector<string>* BoundPair = StringToStrings((*RxnBound)[i],":");
	Reaction* CurrentReaction = InData->FindReaction("NAME;DATABASE;ENTRY",(*BoundPair)[0].data());
		if (CurrentReaction != NULL) {
			BoundMap[(*BoundPair)[0]]["min"] = atof((*BoundPair)[1].data());
			BoundMap[(*BoundPair)[0]]["max"] = atof((*BoundPair)[2].data());
		}
		delete BoundPair;
	}
	
	for (int i=0; i < FNumVariables(); i++) {
		if (GetVariable(i)->Type == FLUX || GetVariable(i)->Type == FORWARD_FLUX || GetVariable(i)->Type == REVERSE_FLUX) {
			if (BoundMap.count(GetVariable(i)->Name) > 0) {

				LinEquation* NewConstraint = InitializeLinEquation();
					NewConstraint->RightHandSide = BoundMap[GetVariable(i)->Name]["min"];
					NewConstraint->EqualityType =GREATER;
					NewConstraint->ConstraintType = LINEAR;
					NewConstraint->ConstraintMeaning.assign("objective constraint");
					NewConstraint->Coefficient.push_back(1);
					NewConstraint->Coefficient.push_back(1);
					NewConstraint->Variables.push_back(GetVariable(i));
					NewConstraint->Variables.push_back(alpha);
					NewConstraint->AssociatedReaction = GetVariable(i)->AssociatedReaction;
					NewConstraint->AssociatedSpecies = GetVariable(i)->AssociatedReaction->GetReactant(i);
				LoadConstToSolver(AddConstraint(NewConstraint));

				LinEquation* NewConstraint2 = InitializeLinEquation();
					NewConstraint2->RightHandSide = BoundMap[GetVariable(i)->Name]["max"];
					NewConstraint2->EqualityType =LESS;
					NewConstraint->ConstraintType = LINEAR;
					NewConstraint2->ConstraintMeaning.assign("objective constraint");
					NewConstraint2->Coefficient.push_back(1);
					NewConstraint2->Coefficient.push_back(-1);
					NewConstraint2->Variables.push_back(GetVariable(i));
					NewConstraint2->Variables.push_back(beta);
					NewConstraint2->AssociatedReaction = GetVariable(i)->AssociatedReaction;
					NewConstraint2->AssociatedSpecies = GetVariable(i)->AssociatedReaction->GetReactant(i);
				LoadConstToSolver(AddConstraint(NewConstraint2));
			} 
		}
	}		

	if (FMax()) {
	  Kappa = -1 * Kappa; // because objective is max.
	}
	LinEquation* NewObjective = CloneLinEquation(GetObjective());
	NewObjective->Variables.push_back(alpha);
	NewObjective->Variables.push_back(beta);
	NewObjective->Coefficient.push_back(Kappa);
	NewObjective->Coefficient.push_back(Kappa);
	AddObjective(NewObjective);
	LoadObjective();	
	//Optimizing objective
	OptSolutionData* Solution = RunSolver(true,true,true);
	if (Solution == NULL) {
		cerr << "No solution found!" << endl;
		return FAIL;
	}
	
	PrintProblemReport(Solution->Objective,Parameters,Note);
	delete RxnBound;

	double objectFraction = NewObjective->Variables[0]->Value / ObjectiveValue;
	ofstream Output;
	if (OpenOutput(Output,FOutputFilepath()+"PROMResult.txt")) {
		Output << NewObjective->Variables[1]->Name <<"\t" <<NewObjective->Variables[1]->Value << endl; //print alpha
		Output << NewObjective->Variables[2]->Name <<"\t" <<NewObjective->Variables[2]->Value << endl; //print beta
		Output << "objectFraction\t" << objectFraction;
		Output.close();
	}
	return SUCCESS;
}

/*FitFitGeneActivtyState
Core function for the method Tintle2014
Author: Shinnosuke Kondo, Hope College, 5/14/2014.
Description: TODO*/
int MFAProblem::FitGeneActivtyState(Data* InData) {
	//Read in optimization parameters
	OptimizationParameter* Parameters = ReadParameters();
	//Adjusting settings for study
	Parameters->DecomposeReversible = true;
	Parameters->ReactionsUse = true;
	Parameters->AllReactionsUse = true;
	Parameters->GeneConstraints = true;

	//Building problem
	if (BuildMFAProblem(InData, Parameters) != SUCCESS) {
		cerr << "Could not build gene activity state problem!" << endl;
		return FAIL;
	}
	//Checking that model grows (And set up variables for later use.)
	string Note;
	double ObjectiveValue = 0;
	if (OptimizeSingleObjective(InData, Parameters, GetParameter("objective"),
			false, false, ObjectiveValue, Note) != SUCCESS) {
		cerr << "Could not run gene activity state assertion problem!" << endl;
		return FAIL;
	}
	if (ObjectiveValue == 0) {
		cerr << "Model did not grow before analysis!" << endl;
		return FAIL;
	}

	LinEquation* ObjectiveConstraint = MakeObjectiveConstraint(0.1*ObjectiveValue,GREATER);
	LoadConstToSolver(ObjectiveConstraint->Index);

	// Now it loads a penalty coefficient for each gene.
	vector<string>* GeneCoef = StringToStrings(GetParameter("Gene Activity State"),";");
	if (GeneCoef->size() <= 1) {
		cerr << "No gene activity state information!" << endl;
		return FAIL;
	}
	double w = atof((*GeneCoef)[0].data());

	map<string,double> CoeffMap;
	map<string,string> CaseMap;
	for (int i=1; i < int(GeneCoef->size()); i++) {
		vector<string>* CoefTrio = StringToStrings((*GeneCoef)[i],":");
		Gene* CurrentGene = InData->FindGene("NAME;DATABASE;ENTRY",(*CoefTrio)[0].data());
		if (CurrentGene != NULL) {
			CoeffMap[(*CoefTrio)[0]] = atof((*CoefTrio)[1].data());
			CaseMap[(*CoefTrio)[0]] = (*CoefTrio)[2];
		}
		delete CoefTrio;
	}
	LinEquation* NewObjective = CloneLinEquation(GetObjective());
	NewObjective->Coefficient[0] = w;
	NewObjective->ConstraintType = LINEAR;
	double fractional_w = (1 - w);
	// we want the contributions of the biomass and the gene penalty to be on equivalent scales, so we 
	// change the coefficient for gene penalty so that it ranges between 0 and the ObjectiveValue (before multiplying by fractional_w).
	fractional_w = ObjectiveValue * fractional_w / 0.5 / CoeffMap.size();
	if (FMax()) {
	  fractional_w = -1* fractional_w; // Because the new objective should be minimized.
	}		

	double originalGrowth = GetObjective()->Variables[0]->Value;
	for (int i=0; i < FNumVariables(); i++) {
	  if (GetVariable(i)->Type == GENE_USE) {
	    if (CoeffMap.count(GetVariable(i)->Name) > 0 && CaseMap[GetVariable(i)->Name] != "2" ) {
		    MFAVariable* NewVariable;		    
		    if(CaseMap[GetVariable(i)->Name] == "1") {
			    //penalized if a gene is active
			    NewVariable = GetVariable(i);
		    } else if (CaseMap[GetVariable(i)->Name] == "3") {
			    //penalized if a gene is inactive
			    // Need to "not" Gene_Use vatiable.
			    MFAVariable* GeneUnuseVariable = InitializeMFAVariable();
			    GeneUnuseVariable->Name = ("Not_" + GetVariable(i)->Name);
			    GeneUnuseVariable->UpperBound = 1;
			    GeneUnuseVariable->LowerBound = 0;
			    GeneUnuseVariable->Binary = true;
			    GeneUnuseVariable->Type = GENE_UNUSE;
			    LoadVariable(AddVariable(GeneUnuseVariable));		      		      
			    
			    LinEquation* NewConstraint = InitializeLinEquation();
			    NewConstraint->RightHandSide = 1;
			    NewConstraint->EqualityType =EQUAL;
			    NewConstraint->ConstraintType = LINEAR;
			    NewConstraint->ConstraintMeaning.assign("Make one variable NOT of the other");
			    NewConstraint->Coefficient.push_back(1);
			    NewConstraint->Variables.push_back(GetVariable(i));
			    NewConstraint->Coefficient.push_back(1);
			    NewConstraint->Variables.push_back(GeneUnuseVariable);		      
			    LoadConstToSolver(AddConstraint(NewConstraint));		      
		
			    NewVariable = GeneUnuseVariable;
		    } else {
			    // Unknown case
			    cerr << "Ignore unknown case " + CaseMap[GetVariable(i)->Name] + " for gene " +  GetVariable(i)->Name + "!" << endl; 
			    continue;
		    }

		    NewObjective->Variables.push_back(NewVariable);
		    NewObjective->Coefficient.push_back(fractional_w * CoeffMap[GetVariable(i)->Name]);		      
	    }
	  }
	}
	AddObjective(NewObjective);
	LoadObjective();
	//Optimizing objective
	OptSolutionData* Solution = RunSolver(true,true,true);
	if (Solution == NULL) {
		cerr << "No solution found!" << endl;
		return FAIL;
	}

	double newobjectivevalue = Solution->Objective;
	PrintProblemReport(newobjectivevalue,Parameters,Note);
	delete GeneCoef;

	ofstream Output;
	if (OpenOutput(Output,FOutputFilepath()+"GeneActivityStateFBAResult.txt")) {
		Output << "Name\tValue" << endl;
		Output << "originalObjective\t" << ObjectiveValue << endl;
		Output << "objective\t" << newobjectivevalue << endl;
		Output << "originalGrowth\t" << originalGrowth << endl;
		Output << "growth\t" << NewObjective->Variables[0]->Value << endl;
		for (int i=1; i < NewObjective->Variables.size(); i++) {
			Output << NewObjective->Variables[i]->Name << "\t" << NewObjective->Variables[i]->Value << endl;
		}
		for (int i=0; i < NewObjective->QuadOne.size(); i++) {
			Output << NewObjective->QuadOne[i]->Name << "\t" << NewObjective->QuadOne[i]->Value << endl;
		}
		for (int i=0; i < NewObjective->QuadTwo.size(); i++) {
			Output << NewObjective->QuadTwo[i]->Name << "\t" << NewObjective->QuadTwo[i]->Value << endl;
		}

	  Output.close();
	}
	return SUCCESS;
}

/*GenerateMinimalReactionLists
Author: Christopher Henry, Argonne National Laboratory, 11/15/2009
Description: Given an objective, this function generates a list of every reaction involved in production*/
int MFAProblem::GenerateMinimalReactionLists(Data* InData) {
	//Read in optimization parameters
	OptimizationParameter* Parameters = ReadParameters();
	//Adjusting settings for study
	Parameters->DecomposeReversible = true;
	Parameters->ReactionsUse = true;
	Parameters->AllReactionsUse = true;
	//Building problem
	if (BuildMFAProblem(InData,Parameters) != SUCCESS) {
		cerr << "Could not build problem formulation!" << endl;
		return FAIL;
	}
	//Checking that model grows
	string Note;
	double ObjectiveValue = 0;
	if (OptimizeSingleObjective(InData,Parameters,GetParameter("objective"),false,false,ObjectiveValue,Note) != SUCCESS) {
		cerr << "Could not build problem formulation!" << endl;
		return FAIL;
	}
	if (ObjectiveValue == 0) {
		cerr << "Objective value of zero!" << endl;
		return FAIL;
	}
	//Fixing objective to a nonzero value
	if (Max) {
		LoadConstToSolver(MakeObjectiveConstraint(Parameters->OptimalObjectiveFraction*ObjectiveValue,GREATER)->Index);
	} else {
		LoadConstToSolver(MakeObjectiveConstraint(Parameters->OptimalObjectiveFraction*ObjectiveValue,LESS)->Index);
	}

	vector<int> VariableTypes;
	VariableTypes.push_back(REACTION_USE);
	VariableTypes.push_back(FORWARD_USE);
	VariableTypes.push_back(REVERSE_USE);
	int NumSolutions = RecursiveMILP(InData,Parameters,VariableTypes,true);

	return SUCCESS;
}

int MFAProblem::ParseRegExp(OptimizationParameter* InParameters, Data* InData, string Expression) {
	//Ensuring that the gene string contains some content
	if (Expression.length() == 0) {
		return NULL;
	}
	//Standardizing characters
	string NewExpression = StringReplace(Expression.data(),"|"," or ");
	NewExpression = StringReplace(NewExpression.data(),","," or ");
	NewExpression = StringReplace(NewExpression.data(),"+"," and ");
	NewExpression = StringReplace(NewExpression.data(),"NOT"," ! ");
	NewExpression = StringReplace(NewExpression.data(),"not"," ! ");
	NewExpression = StringReplace(NewExpression.data(),"!","not");
	//Placing spaces around perenthesis
	NewExpression = StringReplace(NewExpression.data(),"("," | ");
	NewExpression = StringReplace(NewExpression.data(),"|"," ( ");
	NewExpression = StringReplace(NewExpression.data(),")"," | ");
	NewExpression = StringReplace(NewExpression.data(),"|"," ) ");
	//Break file line up into words using "space" as a delimiter
	vector<string>* Strings = StringToStrings(NewExpression," ",true);
	//Checking if this is a KO gene. If it is, then the constraints for the gene are NOT created
	for (int i=0; i < int(InParameters->KOGenes.size()); i++) {
		if (InParameters->KOGenes[i].compare((*Strings)[0]) == 0) {
			delete Strings;
			return SUCCESS;
		}
	}
	//Creating constraints for node
	Gene* currentGene = InData->FindGene("DATABASE;NAME",(*Strings)[0].data()); 
	//Adding gene to database if necessary
	if (currentGene == NULL) {
		currentGene = InData->AddGene((*Strings)[0]);
		AddVariable(currentGene->CreateMFAVariable(InParameters));
	}
	//Now we parse the conditions
	int Level = 0;
	bool On = true;
	RegLogicNode* Node = NULL;
	RegLogicNode* NewRoot = NULL;
	vector<RegLogicNode*> NodeAtLevel(100);
	vector<RegLogicNode*> LogicNodes;
	vector<bool> NotsAtLevel(100,true);
	for (int i=1; i < int(Strings->size()); i++) {	
		//Getting the current word from the Strings vector
		string Current = (*Strings)[i];
		//Checking for logic terms first
		if (ConvertToLower(Current).compare("and") == 0 ) {
			Node->Logic = AND;
		} else if (ConvertToLower(Current).compare("or") == 0) {
			Node->Logic = OR;
		} else if (ConvertToLower(Current).compare("(") == 0) {
			NotsAtLevel[Level] = On;
			On = true;
			Level++;
		} else if (ConvertToLower(Current).compare("not") == 0) {
			On = false;
		} else if (ConvertToLower(Current).compare(")") == 0) {
			if (Node != NULL && Node->Level >= 1) {
				//Saving the old node pointer before we overwrite it
				RegLogicNode* OldNode = Node;
				bool NodeOn = NotsAtLevel[OldNode->Level-1];
				NodeAtLevel[OldNode->Level] = NULL;
				//Getting the new node
				Node = NodeAtLevel[OldNode->Level-1];
				NotsAtLevel[OldNode->Level-1] = true;
				if (Node == NULL) {
					Node = new RegLogicNode;
					Node->Level = Level-1;
					if (Node->Level == 0) {
						NewRoot = Node;
					}
					Node->Logic = OR;
					//Saving the new node in the nodes vector
					LogicNodes.push_back(Node);
					//Saving the node at the current level
					NodeAtLevel[OldNode->Level-1] = Node;
				}
				//add the current node to the new logic node
				Node->LogicNodes.push_back(OldNode);
				Node->LogicOn.push_back(NodeOn);
			}
			Level--;
		} else if (Current.find_first_of("abcdefghijklmnopqrstuvwxyz0123456789") != -1) {
			//Instantiating and initializing nodes
			if (Node == NULL || Level > Node->Level) {
				//Creating a new node
				Node = new RegLogicNode;
				Node->Level = Level;
				if (Node->Level == 0) {
					NewRoot = Node;
				}
				Node->Logic = OR;
				//Saving the new node in the nodes vector
				LogicNodes.push_back(Node);
				NodeAtLevel[Node->Level] = Node;
			}
			//Adding object to the logic node
			Node->Object.push_back(Current);
			Node->ObjectOn.push_back(On);
			On = true;
		}
	}
	//Deleting vector
	delete Strings;
	//Checking syntax
	if (Level != 0) {
		if (Level > 0) {
			cerr << "Missing ) on line " << NewExpression << endl;
		} else {
			cerr << "Missing ( on line " << NewExpression << endl;
		}
	}
	//Propagating the nots so only objects have nots
	Node = NewRoot;
	vector<RegLogicNode*> NodesToExplore;
	int Position = 0;
	while (Node != NULL) {
		for (int i=0; i < int(Node->LogicNodes.size()); i++) {
			NodesToExplore.push_back(Node->LogicNodes[i]);
			if (Node->LogicOn[i] == false) {
				Node->LogicOn[i] = true;
				//Changing the logic of the subnode
				if (Node->LogicNodes[i]->Logic == AND) {
					Node->LogicNodes[i]->Logic = OR;
				} else {
					Node->LogicNodes[i]->Logic = AND;
				}
				//Reversing all the "ons" in the subnode
				for (int j=0; j < int(Node->LogicNodes[i]->LogicNodes.size()); j++) {
					if (Node->LogicNodes[i]->LogicOn[j] == true) {
						Node->LogicNodes[i]->LogicOn[j] = false;
					} else {
						Node->LogicNodes[i]->LogicOn[j] = true;
					}
				}
				for (int j=0; j < int(Node->LogicNodes[i]->Object.size()); j++) {
					if (Node->LogicNodes[i]->ObjectOn[j] == true) {
						Node->LogicNodes[i]->ObjectOn[j] = false;
					} else {
						Node->LogicNodes[i]->ObjectOn[j] = true;
					}
				}
			}
		}
		if (Position < int(NodesToExplore.size())) {
			Node = NodesToExplore[Position];
		} else {
			Node = NULL;
		}
		Position++;
	}
	//Consolidating nodes
	if (NewRoot != NULL) {
		bool Change = true;
		for (int i=0; i < int(LogicNodes.size()); i++) {
			if (LogicNodes[i] != NULL) {
				Change = false;
				for (int j=0; j < int(LogicNodes[i]->LogicNodes.size()); j++) {
					RegLogicNode* Temp = LogicNodes[i]->LogicNodes[j];
					if ((Temp->Object.size()+Temp->LogicNodes.size()) == 1 || Temp->Logic ==  LogicNodes[i]->Logic) {
						Change = true;
						for (int k=0; k < int(Temp->Object.size()); k++) {
							LogicNodes[i]->Object.push_back(Temp->Object[k]);
							LogicNodes[i]->ObjectOn.push_back(Temp->ObjectOn[k]);
						}
						for (int k=0; k < int(Temp->LogicNodes.size()); k++) {
							LogicNodes[i]->LogicNodes.push_back(Temp->LogicNodes[k]);
							LogicNodes[i]->LogicOn.push_back(Temp->LogicOn[k]);
						}
						for (int k=0; k < int(LogicNodes.size()); k++) {
							if (LogicNodes[k] == Temp) {
								delete LogicNodes[k];
								LogicNodes[k] = NULL;
							}
						}
						LogicNodes[i]->LogicNodes.erase(LogicNodes[i]->LogicNodes.begin()+j,LogicNodes[i]->LogicNodes.begin()+j+1);
						LogicNodes[i]->LogicOn.erase(LogicNodes[i]->LogicOn.begin()+j,LogicNodes[i]->LogicOn.begin()+j+1);
						j--;
					}
				}
				if (Change) {
					i = 0;
				}
			}
		}
		Change = true;
		while (Change) {
			Change = false;
			for (int j=0; j < int(NewRoot->LogicNodes.size()); j++) {
				if (NewRoot->LogicNodes[j]->Logic == NewRoot->Logic || (NewRoot->LogicNodes.size() == 1 && NewRoot->Object.size() == 0)) {
					if (NewRoot->LogicNodes.size() == 1 && NewRoot->Object.size() == 0) {
						NewRoot->Logic = NewRoot->LogicNodes[j]->Logic;
					}
					Change = true;
					for (int k=0; k < int(NewRoot->LogicNodes[j]->Object.size()); k++) {
						NewRoot->Object.push_back(NewRoot->LogicNodes[j]->Object[k]);
						NewRoot->ObjectOn.push_back(NewRoot->LogicNodes[j]->ObjectOn[k]);
					}
					for (int k=0; k < int(NewRoot->LogicNodes[j]->LogicNodes.size()); k++) {
						NewRoot->LogicNodes.push_back(NewRoot->LogicNodes[j]->LogicNodes[k]);
						NewRoot->LogicOn.push_back(NewRoot->LogicNodes[j]->LogicOn[k]);
					}
					for (int k=0; k < int(LogicNodes.size()); k++) {
						if (LogicNodes[k] == NewRoot->LogicNodes[j]) {
							delete LogicNodes[k];
							LogicNodes[k] = NULL;
						}
					}
					NewRoot->LogicNodes.erase(NewRoot->LogicNodes.begin()+j,NewRoot->LogicNodes.begin()+j+1);
					NewRoot->LogicOn.erase(NewRoot->LogicOn.begin()+j,NewRoot->LogicOn.begin()+j+1);
					j--;
				}
			}
		}
	}

	//Creating variables for all nodes but the root node
	MFAVariable* Var;
	map<RegLogicNode*,MFAVariable*> NodeVariable;
	string GeneLabel = currentGene->GetData("DATABASE",STRING);
	for (int i=0; i < int(LogicNodes.size()); i++) {
		if (LogicNodes[i] != NULL && LogicNodes[i] != NewRoot) {
			Var = InitializeMFAVariable();
			Var->LowerBound=0;
			Var->UpperBound=1;
			Var->Type=COMPLEX_USE;
			Var->Binary=true;
			Var->Name.assign(GeneLabel+"_LN");
			Var->Name += itoa(i);
			AddVariable(Var);
			NodeVariable[LogicNodes[i]] = Var;
		}
	}
	
	//Creating the constraints
	for (int i=0; i < int(LogicNodes.size()); i++) {
		//Checking that the logic node still exists
		if (LogicNodes[i] != NULL) {
			string Label;
			if (LogicNodes[i] == NewRoot) {
				Label = GeneLabel;
				Var = currentGene->GetMFAVar();
			} else {
				Label = GeneLabel+"_LN"+itoa(i);
				Var = NodeVariable[LogicNodes[i]];
			}
			int LHS = 0;
			int UHS = 0;
			int Coef = 1;
			int totalEntities = int(LogicNodes[i]->Object.size()+LogicNodes[i]->LogicNodes.size());
			if (LogicNodes[i]->Logic == AND) {
				LHS = -1;
				UHS = int(2*totalEntities-1);
				Coef = 2;
			}
			LinEquation* NewLowerConstraint = InitializeLinEquation(("Regulatory Lower Constraint "+Label).data(),LHS,GREATER);
			LinEquation* NewUpperConstraint = InitializeLinEquation(("Regulatory Upper Constraint "+Label).data(),UHS,LESS);
			NewLowerConstraint->Variables.push_back(Var);
			NewUpperConstraint->Variables.push_back(Var);
			if (LogicNodes[i]->Logic == OR) {
				NewLowerConstraint->Coefficient.push_back(-1);
				NewUpperConstraint->Coefficient.push_back(-1*totalEntities);
			} else {
				NewLowerConstraint->Coefficient.push_back(-2*totalEntities);
				NewUpperConstraint->Coefficient.push_back(-2*totalEntities);
			}
			//Adding the subentities of the logic node
			for(int j=0; j < int(LogicNodes[i]->Object.size());j++ ){
				int sign = 1;
				if (LogicNodes[i]->ObjectOn[j] == false) {
					sign = -1;
					NewLowerConstraint->RightHandSide+= -Coef;
					NewUpperConstraint->RightHandSide+= -Coef;
				}
				AddVariableToRegulationConstraint(NewLowerConstraint,sign*Coef,LogicNodes[i]->Object[j],InData,InParameters);
				AddVariableToRegulationConstraint(NewUpperConstraint,sign*Coef,LogicNodes[i]->Object[j],InData,InParameters);
			}
			for(int j=0; j < int(LogicNodes[i]->LogicNodes.size());j++ ){
				int sign = 1;
				if (LogicNodes[i]->LogicOn[j] == false) {
					sign = -1;
					NewLowerConstraint->RightHandSide+= -Coef;
					NewUpperConstraint->RightHandSide+= -Coef;
				}
				Var = NodeVariable[LogicNodes[i]->LogicNodes[j]];
				NewLowerConstraint->Variables.push_back(Var);
				NewLowerConstraint->Coefficient.push_back(sign*Coef);
				NewUpperConstraint->Variables.push_back(Var);
				NewUpperConstraint->Coefficient.push_back(sign*Coef);
			}
			//Adding the constraints
			AddConstraint(NewLowerConstraint);
			AddConstraint(NewUpperConstraint);
		}
	}

	for (int i=0; i < int(LogicNodes.size()); i++) {
		if (LogicNodes[i] != NULL) {
			delete LogicNodes[i];
		}
	}

	return SUCCESS;
}

int MFAProblem::AddRegulatoryConstraints(OptimizationParameter* InParameters, Data* InData) {
	//Parse the input file
	ifstream input;
	if (!OpenInput(input,GetParameter("Regulatory constraint file"))) {
		cerr << "Could not read regulatory constraint file " << GetParameter("Regulatory constraint file") << endl;
		return FAIL;
	}
	//Parsing the file
	while (!input.eof()) {
		//Read in one line from file
		string Line = GetFileLine(input);
		//Parsing the line into a logic node
		string Gene;
		if (ParseRegExp(InParameters, InData,Line) != SUCCESS) {
			cerr << "Could not parse regulation expressions: " << Line << endl;
		}
	}
	input.close();
	return SUCCESS;
}

void MFAProblem::AddVariableToRegulationConstraint(LinEquation* InEquation,double Coefficient,string VariableName,Data* InData,OptimizationParameter* InParameters) {
	int Value = int(FLAG);
	MFAVariable* Var = NULL;
	string VariableArg = "";
	if (VariableName.find("{") != -1) {
		vector<string>* Array = StringToStrings(VariableName,"{");
		VariableName = (*Array)[0];
		VariableArg = (*Array)[1].substr(0,(*Array)[1].length()-1);
	}
	if (VariableName.compare("CONDITION") == 0) {
		if (VariableArg.compare("on") == 0) {
			Value = 1;
		} else if (VariableArg.compare("off") == 0) {
			Value = 0;
		} else if (InParameters->Conditions.count(VariableArg) > 0) {
			Value = int(InParameters->Conditions[VariableArg]);
		} else {
			FErrorFile() << VariableArg << " condition not provided in " << GetParameter("Gene dictionary") << " file." << endl;
			FlushErrorFile();
			Value = 0;
		}
	} else {
		Species* CurrSpec = InData->FindSpecies("DATABASE;NAME",VariableName.data());
		if (CurrSpec != NULL) {
			if (VariableArg.length() == 0 || VariableArg.compare(">0") == 0) {
				Var = CurrSpec->GetMFAVar(FORWARD_DRAIN_USE,GetCompartment("e")->Index);
				if (Var == NULL) {
					Var = CurrSpec->GetMFAVar(DRAIN_USE,GetCompartment("e")->Index);
				}
				if (Var == NULL) {
					Var = CurrSpec->GetMFAVar(FORWARD_DRAIN_USE,GetCompartment("c")->Index);
				}
				if (Var == NULL) {
					Var = CurrSpec->GetMFAVar(DRAIN_USE,GetCompartment("c")->Index);
				}
				if (Var == NULL) {
					Value = 0;
				}
			} else if (VariableArg.compare("<0") == 0) {
				Var = CurrSpec->GetMFAVar(REVERSE_DRAIN_USE,GetCompartment("e")->Index);
				if (Var == NULL) {
					Var = CurrSpec->GetMFAVar(REVERSE_DRAIN_USE,GetCompartment("c")->Index);
				}
				if (Var == NULL) {
					Value = 0;
				}
			}
		}
		if (Var == NULL) {
			Reaction* CurrReact = InData->FindReaction("DATABASE;NAME",VariableName.data());
			if (CurrReact != NULL) {
				if (VariableArg.length() == 0 || VariableArg.compare(">0") == 0) {
					Var = CurrReact->GetMFAVar(FORWARD_USE);
					if (Var == NULL) {
						Var = CurrReact->GetMFAVar(REACTION_USE);
					}
					if (Var == NULL) {
						Value = 0;
					}
				} else if (VariableArg.compare("<0") == 0) {
					Var = CurrReact->GetMFAVar(REVERSE_USE);
					if (Var == NULL) {
						Value = 0;
					}
				}
			}
		}
		if (Var == NULL) {
			Gene* CurrGene = InData->FindGene("DATABASE;NAME",VariableName.data());
			if (CurrGene != NULL) {
				Var = CurrGene->GetMFAVar();
			} else if (VariableArg.length() == 0) {
				CurrGene = InData->AddGene(VariableName);
				CurrGene->CreateMFAVariable(InParameters);
				Var = CurrGene->GetMFAVar();
				AddVariable(Var);
			}
		}
	}

	if (Value != FLAG) {
		InEquation->RightHandSide = InEquation->RightHandSide - Coefficient*Value;
		return;
	}
	if (Var == NULL) {
		cout << VariableName << endl;
	}
	InEquation->Variables.push_back(Var);
	InEquation->Coefficient.push_back(Coefficient);
}

//File IO Functions
void MFAProblem::PrintProblemReport(double SingleObjective,OptimizationParameter* InParameters, string InNote) {
	ofstream Output;
	string Filename(FOutputFilepath());
	Filename.append(GetParameter("MFA problem report filename"));
	if (!OpenOutput(Output,Filename,true)) {
		ProblemIndex++;
		return;
	}
	
	if (ProblemIndex == 0) {
		Output << "Index;Objective;Notes;Time;Mass balance constraints;Thermodynamic constraints;Thermodynamic uncertainty;Lumped reactions;All reversible;Load foreign database;Minimize foreign reactions;Minimize reactions and maximize objective;Maximum objective fraction;Solution size interval;Exchanged species;Media file;Media compounds;Find tight bounds;Load tight bounds;PNum;NNum;PVNum;NVNum;VNum;BNum;P;N;PV;NV;V;B;MILP solutions;MILP objectives;Individual metabolites with zero production;Individual metablite production" << endl;
	}

	Output << ProblemIndex << ";" << SingleObjective << ";" << InNote << ";" << ElapsedTime(MFAProblemClockIndex) << ";";
	if (InParameters->MassBalanceConstraints) {
		Output << "yes;";
	} else {
		Output << "no;";
	}

	if (InParameters->ThermoConstraints) {
		Output << "yes;";
	} else {
		Output << "no;";
	}

	if (InParameters->DeltaGError) {
		Output << "delta G;";
	} else {
		Output << "none;";
	}

	if (InParameters->AddLumpedReactions) {
		Output << "yes;";
	} else {
		Output << "no;";
	}

	if (InParameters->AllReversible) {
		Output << "yes;";
	} else {
		Output << "no;";
	}

	if (InParameters->LoadForeignDB) {
		Output << GetParameter("Filename for foreign reaction database") << ";";
	} else {
		Output << "no;";
	}

	if (InParameters->MinimizeForeignReactions) {
		Output << "yes;";
	} else {
		Output << "no;";
	}

	if (InParameters->SimultaneouslyMinReactionsMaxObjective) {
		Output << "yes;";
	} else {
		Output << "no;";
	}

	Output << InParameters->OptimalObjectiveFraction << ";" << InParameters->SolutionSizeInterval << ";";

	for (int i=0; i < int(InParameters->ExchangeSpecies.size()); i++) {
		Output << InParameters->ExchangeSpecies[i];
		if (i < int(InParameters->ExchangeSpecies.size()-1)) {
			Output << "|";	
		}
	}
	string MediaFile = RemoveExtension(GetParameter("user bounds filename"));
	Output << ";" << MediaFile << ";";
	for (int i=0; i < int(InParameters->UserBounds->VarName.size()); i++) {
		if (InParameters->UserBounds->VarCompartment[i].compare("none") == 0 && InParameters->UserBounds->VarType[i] == DRAIN_FLUX && InParameters->UserBounds->VarMax[i] > 0) {
			Output << InParameters->UserBounds->VarName[i] << "|";
		}
	}
	if (GetParameter("find tight bounds").compare("1") == 0) {
		Output << ";yes;";
	} else {
		Output << ";no;";
	}	
	if (InParameters->LoadTightBounds) {
		Output << "yes;";
	} else {
		Output << "no;";
	}

	//The order of the classes in this vector is:P,N,PV,NV,V,B
	vector <vector<Reaction*> > ReactionClasses;
	ReactionClasses.resize(6);
	if (GetParameter("find tight bounds").compare("1") == 0) {
		Data* MyData = NULL;
		for (int i=0; i < FNumVariables(); i++) {
			if (GetVariable(i)->AssociatedReaction != NULL && GetVariable(i)->AssociatedReaction->FMainData() != NULL) {
				MyData = GetVariable(i)->AssociatedReaction->FMainData();
				i = FNumVariables();
			}
		}

		if (MyData != NULL) {
			for (int i=0; i < MyData->FNumReactions(); i++) {
				ReactionClasses[MyData->GetReaction(i)->GetReactionClass()].push_back(MyData->GetReaction(i));
			}
		}
	}
	
	for (int i=0; i < int(ReactionClasses.size()); i++) {
		Output << ReactionClasses[i].size() << ";";
	}

	for (int i=0; i < int(ReactionClasses.size()); i++) {
		for (int j=0; j < int(ReactionClasses[i].size()); j++) {
			Output << ReactionClasses[i][j]->GetData("DATABASE",STRING) << "|";
		}
		Output << ";";
	}
	
	string ZeroProductionMetabolites;
	string MetaboliteProduction;
	vector<double> MILPObjectives;
	for (int i=0; i < FNumSolutions(); i++) {
		if (GetSolution(i)->Notes.compare("Recursive milp solution") == 0) {
			MILPObjectives.push_back(GetSolution(i)->Objective);
		}
		string Temp("Optimize metabolite production: ");
		int Location = int(GetSolution(i)->Notes.find(Temp));
		if (Location != -1) {
			MetaboliteProduction.append(GetSolution(i)->Notes.substr(Location+Temp.length(),GetSolution(i)->Notes.length()-Location-Temp.length()));
			MetaboliteProduction.append(":");
			MetaboliteProduction.append(dtoa(GetSolution(i)->Objective));
			MetaboliteProduction.append("|");
			if (fabs(GetSolution(i)->Objective) < MFA_ZERO_TOLERANCE) {
				ZeroProductionMetabolites.append(GetSolution(i)->Notes.substr(Location+Temp.length(),GetSolution(i)->Notes.length()-Location-Temp.length()));
				ZeroProductionMetabolites.append("|");
			}
		}
	}

	Output << MILPObjectives.size() << ";";
	
	for (int i=0; i < int(MILPObjectives.size()); i++) {
		Output << MILPObjectives[i] << "|";
	}

	Output << ";" << ZeroProductionMetabolites << ";" << MetaboliteProduction << endl;
	Output.close();
	if (FNumSolutions() > 0) {
		if (InParameters->PrintSolutions) {
			PrintSolutions(-1,-1);
		}
		if (InParameters->ClearSolutions || MetaboliteProduction.length() > 0) {
			ClearSolutions();
		}
	}
	ProblemIndex++;
	ClearClock(MFAProblemClockIndex);
	MFAProblemClockIndex = StartClock(MFAProblemClockIndex);
}	

int MFAProblem::LoadTightBounds(Data* InData, bool SetBoundToTightBounds) {
	string Filename = GetDatabaseDirectory(true)+GetParameter("Tight bounds input filename");
	
	FileBounds* TightBounds = ReadBounds(Filename.data());

	if (TightBounds == NULL) {
		FErrorFile() << "Failed to read in tight bounds from " << Filename << endl;
		FlushErrorFile();
		return FAIL;
	}

	int Result = ApplyInputBounds(TightBounds,InData,true);

	LoosenBounds(TightBounds);

	Result = ApplyInputBounds(TightBounds,InData);
	delete TightBounds;
	return Result;
}

void MFAProblem::SaveTightBounds() {
	//Printing the raw solution data
	Data* SourceDatabase = NULL;
	vector<int> ReactionVariables;
	vector<int> CompoundVariables;
	vector<bool> Includes(7,false);
	ofstream Output;
	string Filename(FOutputFilepath());
	Filename.append("MFAOutput/RawData/RawTightBounds");
	Filename.append(itoa(ProblemIndex));
	Filename.append(".txt");
	if (!OpenOutput(Output,Filename)) {
		return;
	}
	Output << "VarName;VarType;VarCompartment;VarMin;VarMax" << endl;
	for (int i=0; i < FNumVariables(); i++) {
		if (GetVariable(i)->Max != FLAG) {
			if (GetVariable(i)->Type == DELTAG) {
				if (!Includes[0]) {
					Includes[0] = true;
					ReactionVariables.push_back(DELTAG);
				}
			} else if (GetVariable(i)->Type == REACTION_DELTAG_ERROR) {
				if (!Includes[1]) {
					Includes[1] = true;
					ReactionVariables.push_back(REACTION_DELTAG_ERROR);
				}
			} else if (GetVariable(i)->Type == FORWARD_FLUX || GetVariable(i)->Type == REVERSE_FLUX || GetVariable(i)->Type == FLUX) {
				if (!Includes[2]) {
					Includes[2] = true;
					ReactionVariables.push_back(FLUX);
				}
			} else if (GetVariable(i)->Type == CONC || GetVariable(i)->Type == LOG_CONC) {
				if (!Includes[3]) {
					Includes[3] = true;
					CompoundVariables.push_back(LOG_CONC);
				}
			} else if (GetVariable(i)->Type == POTENTIAL) {
				if (!Includes[4]) {
					Includes[4] = true;
					CompoundVariables.push_back(POTENTIAL);
				}
			} else if (GetVariable(i)->Type == FORWARD_DRAIN_FLUX || GetVariable(i)->Type == REVERSE_DRAIN_FLUX || GetVariable(i)->Type == DRAIN_FLUX) {
				if (!Includes[5]) {
					Includes[5] = true;
					CompoundVariables.push_back(DRAIN_FLUX);
				}
			} else if (GetVariable(i)->Type == DELTAGF_PERROR || GetVariable(i)->Type == DELTAGF_NERROR || GetVariable(i)->Type == DELTAGF_ERROR) {
				if (!Includes[6]) {
					Includes[6] = true;
					CompoundVariables.push_back(DELTAGF_ERROR);
				}
			}

			if (GetVariable(i)->AssociatedSpecies != NULL) {
				SourceDatabase = GetVariable(i)->AssociatedSpecies->FMainData();
				Output << GetVariable(i)->AssociatedSpecies->GetData("DATABASE",STRING) << ";";
			} else if (GetVariable(i)->AssociatedReaction != NULL) {
				SourceDatabase = GetVariable(i)->AssociatedReaction->FMainData();
				Output << GetVariable(i)->AssociatedReaction->GetData("DATABASE",STRING) << ";";
			} else {
				Output << i+1 << ";";
			}
			Output << ConvertVariableType(GetVariable(i)->Type) << ";";
			if (GetCompartment(GetVariable(i)->Compartment) == NULL) {
				Output << "none" << ";";
			} else {
				Output << GetCompartment(GetVariable(i)->Compartment)->Abbreviation << ";";
			}
			if (fabs(GetVariable(i)->Min) < MFA_ZERO_TOLERANCE) {
				Output << 0 << ";";
			} else {
				Output << GetVariable(i)->Min << ";";
			}
			if (fabs(GetVariable(i)->Max) < MFA_ZERO_TOLERANCE) {
				Output << 0 << endl;
			} else {
				Output << GetVariable(i)->Max << endl;
			}
		}
	}
	Output.close();
	
	//Printing the reaction solution data
	Filename.assign(FOutputFilepath());
	Filename.append("MFAOutput/TightBoundsReactionData");
	Filename.append(".txt");
	if (!OpenOutput(Output,Filename)) {
		return;
	}
	
	//Printing the heading
	Output << "REACTIONS" << endl;
	Output << "ENTRY;DATABASE ID;EQUATION";
	for (int i=0; i < int(ReactionVariables.size()); i++) {
		Output << ";Lower " << ConvertVariableType(ReactionVariables[i]) << " bound;Upper " << ConvertVariableType(ReactionVariables[i]) << " bound" << ";Min " << ConvertVariableType(ReactionVariables[i]) << ";Max " << ConvertVariableType(ReactionVariables[i]);
	}
	Output << endl;
	//Printing the actual reaction tight bound data
	for (int i=0; i < SourceDatabase->FNumReactions(); i++) {
		Output << SourceDatabase->GetReaction(i)->FEntry() << ";" << SourceDatabase->GetReaction(i)->GetData("DATABASE",STRING) << ";" << SourceDatabase->GetReaction(i)->Query("DEFINITION");
		for (int k=0; k < int(ReactionVariables.size()); k++) {
			vector<double> Temp = SourceDatabase->GetReaction(i)->RetrieveData(ReactionVariables[k],NULL);
			Output << ";" << Temp[1] << ";" << Temp[0] << ";" << Temp[3] << ";" << Temp[2];
		}
		Output << endl;
	}
	Output.close();

	//Printing the compound tight bound data
	Filename.assign(FOutputFilepath());
	Filename.append("MFAOutput/TightBoundsCompoundData");
	Filename.append(".txt");
	if (!OpenOutput(Output,Filename)) {
		return;
	}
	//Printing the heading for the compound tight bound data
	Output << "COMPOUNDS" << endl;
	Output << "ENTRY;DATABASE ID;NAME;FORMULA;COMPARTMENT";
	for (int i=0; i < int(CompoundVariables.size()); i++) {
		Output << ";Lower " << ConvertVariableType(CompoundVariables[i]) << " bound;Upper " << ConvertVariableType(CompoundVariables[i]) << " bound" << ";Min " << ConvertVariableType(CompoundVariables[i]) << ";Max " << ConvertVariableType(CompoundVariables[i]);
	}
	Output << endl;
	//Printing the actual compound tight bound data
	for (int i=0; i < SourceDatabase->FNumSpecies(); i++) {
		for (int m=0; m < SourceDatabase->GetSpecies(i)->FNumCompartments(); m++) {
			Output << SourceDatabase->GetSpecies(i)->FEntry() << ";" << SourceDatabase->GetSpecies(i)->GetData("DATABASE",STRING) << ";" << SourceDatabase->GetSpecies(i)->GetData("NAME",STRING) << ";" << SourceDatabase->GetSpecies(i)->FFormula() << ";" << SourceDatabase->GetSpecies(i)->GetSpeciesCompartment(m)->Compartment->Abbreviation;
			for (int k=0; k < int(CompoundVariables.size()); k++) {
				vector<double> Temp = SourceDatabase->GetSpecies(i)->RetrieveData(CompoundVariables[k],SourceDatabase->GetSpecies(i)->GetSpeciesCompartment(m)->Compartment->Index,NULL);
				Output << ";" << Temp[1] << ";" << Temp[0] << ";" << Temp[3] << ";" << Temp[2];
			}
			Output << endl;
		}
	}
	Output.close();
}

void MFAProblem::PrintSolutions(int StartIndex, int EndIndex,bool tightbounds) {
	string FilenameSuffix;
	if (tightbounds) {
		FilenameSuffix.assign("TB");
	}
	if (StartIndex < 0) {
		StartIndex = 0;
	}
	if (EndIndex >= FNumSolutions() || EndIndex < 0) {
		EndIndex = FNumSolutions();
	}
	//Printing the raw solution data
	ofstream Output;
	string Filename(FOutputFilepath());
	Filename.append("MFAOutput/RawData/RawSolutions");
	Filename.append(FilenameSuffix);
	//Filename.append(itoa(ProblemIndex));
 	Filename.append(".txt");
	if (!OpenOutput(Output,Filename)) {
		return;
	}
	vector<Reaction*> reactions;
	vector<Species*> compounds;
	vector<string> compartments;
	compartments.push_back("none");
	map<string,int,std::less<string> > compartmentIndecies;
	compartmentIndecies["none"] = 0;
	map<string,int,std::less<string> > reactionIndecies;
	map<string,int,std::less<string> > compoundIndecies;
	map<int, vector< vector<bool> >, std::less<int> > cpdVarTypePresence;
	map<int, vector< vector<bool> >, std::less<int> > rxnVarTypePresence;
	map<string, map<int, vector< vector<double> >, std::less<int> >, std::less<string> > cpdVars;
	map<string, map<int, vector< vector<double> >, std::less<int> >, std::less<string> > rxnVars;
	Output << "Index;Type;Compartment;Upper;Lower;Name;Data";	
	for (int j=StartIndex; j < EndIndex; j++) {
		if (GetSolution(j)->Status == SUCCESS) {
			Output << ";Solution " << j;
		}
	}
	Output << endl;
	Output << "-1;OBJECTIVE;none;--;--;Objective function";
	for (int j=StartIndex; j < EndIndex; j++) {
		if (GetSolution(j)->Status == SUCCESS) {
			Output << ";" << GetSolution(j)->Objective;
		}
	}
	Output << endl;
	int reactionCount = 0;
	int compoundCount = 0;
	for (int i=0; i < FNumVariables(); i++) {
		Output << i << ";" << ConvertVariableType(GetVariable(i)->Type) << ";";
		CellCompartment* VarComp = GetCompartment(GetVariable(i)->Compartment);
		string comp("none");
		if (VarComp != NULL) {
			Output << VarComp->Abbreviation << ";" <<  GetVariable(i)->UpperBound << ";" << GetVariable(i)->LowerBound << ";";
			comp = VarComp->Abbreviation;
			if (compartmentIndecies.count(comp) == 0) {
				compartmentIndecies[comp] = compartments.size();
				compartments.push_back(comp);
			}
		} else {
			Output << "none;" <<  GetVariable(i)->UpperBound << ";" << GetVariable(i)->LowerBound << ";";
		}
		if (GetVariable(i)->AssociatedReaction != NULL) {
			if (reactionIndecies.count(GetVariable(i)->AssociatedReaction->GetData("DATABASE",STRING)+comp) == 0) {
				reactions.push_back(GetVariable(i)->AssociatedReaction);
				reactionIndecies[GetVariable(i)->AssociatedReaction->GetData("DATABASE",STRING)+comp] = reactionCount;
				reactionCount++;
			}
			Output << GetVariable(i)->AssociatedReaction->GetData("DATABASE",STRING);
		} else if (GetVariable(i)->AssociatedSpecies != NULL) {
			if (compoundIndecies.count(GetVariable(i)->AssociatedSpecies->GetData("DATABASE",STRING)+comp) == 0) {
				if (comp.compare("b") != 0) {
					compounds.push_back(GetVariable(i)->AssociatedSpecies);
					compoundIndecies[GetVariable(i)->AssociatedSpecies->GetData("DATABASE",STRING)+comp] = compoundCount;
					compoundCount++;
				}
			}
			Output << GetVariable(i)->AssociatedSpecies->GetData("DATABASE",STRING);
		}
		int index = -1;
		int compIndex = compartmentIndecies[comp];
		int sign = 1;
		if (GetVariable(i)->Type == DRAIN_FLUX || GetVariable(i)->Type == FORWARD_DRAIN_FLUX || GetVariable(i)->Type == REVERSE_DRAIN_FLUX) {
			if ( GetVariable(i)->Type == REVERSE_DRAIN_FLUX) {
				sign = -1;
			}
			index = 0;
		} else if (GetVariable(i)->Type == CONC || GetVariable(i)->Type == LOG_CONC) {
			index = 1;
		} else if (GetVariable(i)->Type == POTENTIAL) {
			index = 2;
		} else if (GetVariable(i)->Type == DELTAGF_ERROR || GetVariable(i)->Type == DELTAGF_PERROR || GetVariable(i)->Type == DELTAGF_NERROR) {
			if ( GetVariable(i)->Type == DELTAGF_NERROR) {
				sign = -1;
			}
			index = 3;
		} else if (GetVariable(i)->Type == FLUX || GetVariable(i)->Type == FORWARD_FLUX || GetVariable(i)->Type == REVERSE_FLUX) {
			if ( GetVariable(i)->Type == REVERSE_FLUX) {
				sign = -1;
			}
			index = 0;
		} else if (GetVariable(i)->Type == DELTAG) {
			index = 1;
		} else if (GetVariable(i)->Type == REACTION_DELTAG_ERROR || GetVariable(i)->Type == REACTION_DELTAG_PERROR || GetVariable(i)->Type == REACTION_DELTAG_NERROR) {
			if ( GetVariable(i)->Type == REACTION_DELTAG_NERROR) {
				sign = -1;
			}
			index = 2;
		}

			for (int j=StartIndex; j < EndIndex; j++) {
				if (GetSolution(j)->Status == SUCCESS && i < int(GetSolution(j)->SolutionData.size())) {
					Output << ";" << GetSolution(j)->SolutionData[i];
					
				  if (index != -1) {
					if (GetVariable(i)->AssociatedReaction != NULL) {
						if (int(rxnVarTypePresence[j].size()) == 0) {
							rxnVarTypePresence[j].resize(4);
						}
						if (int(rxnVars[GetVariable(i)->AssociatedReaction->GetData("DATABASE",STRING)][j].size()) == 0) {
							rxnVars[GetVariable(i)->AssociatedReaction->GetData("DATABASE",STRING)][j].resize(4);
						}
						if (int(rxnVarTypePresence[j][index].size()) <= compartmentIndecies[comp]) {
							rxnVarTypePresence[j][index].resize(compartments.size(),false);
						}
						if (int(rxnVars[GetVariable(i)->AssociatedReaction->GetData("DATABASE",STRING)][j][index].size()) <= compartmentIndecies[comp]) {
							rxnVars[GetVariable(i)->AssociatedReaction->GetData("DATABASE",STRING)][j][index].resize(compartments.size(),FLAG);
						}
						if (i < int(GetSolution(j)->SolutionData.size())) {
							if (rxnVars[GetVariable(i)->AssociatedReaction->GetData("DATABASE",STRING)][j][index][compartmentIndecies[comp]] == FLAG) {
								rxnVars[GetVariable(i)->AssociatedReaction->GetData("DATABASE",STRING)][j][index][compartmentIndecies[comp]] = sign*GetSolution(j)->SolutionData[i];
							} else {
								rxnVars[GetVariable(i)->AssociatedReaction->GetData("DATABASE",STRING)][j][index][compartmentIndecies[comp]] += sign*GetSolution(j)->SolutionData[i];
							}
						}
						rxnVarTypePresence[j][index][compartmentIndecies[comp]] = true;
					} else if (GetVariable(i)->AssociatedSpecies != NULL) {
						if (int(cpdVarTypePresence[j].size()) == 0) {
							cpdVarTypePresence[j].resize(4);
						}
						if (int(cpdVarTypePresence[j][index].size()) <= compartmentIndecies[comp]) {
							cpdVarTypePresence[j][index].resize(compartments.size(),false);
						}
						if (int(cpdVars[GetVariable(i)->AssociatedSpecies->GetData("DATABASE",STRING)][j].size()) == 0) {
							cpdVars[GetVariable(i)->AssociatedSpecies->GetData("DATABASE",STRING)][j].resize(4);
						}
						if (int(cpdVars[GetVariable(i)->AssociatedSpecies->GetData("DATABASE",STRING)][j][index].size()) <= compartmentIndecies[comp]) {
							cpdVars[GetVariable(i)->AssociatedSpecies->GetData("DATABASE",STRING)][j][index].resize(compartments.size(),FLAG);
						}
						if (i < int(GetSolution(j)->SolutionData.size())) {
							if (GetVariable(i)->Type == LOG_CONC) {
								cpdVars[GetVariable(i)->AssociatedSpecies->GetData("DATABASE",STRING)][j][index][compartmentIndecies[comp]] = exp(GetSolution(j)->SolutionData[i]);
							} else if (cpdVars[GetVariable(i)->AssociatedSpecies->GetData("DATABASE",STRING)][j][index][compartmentIndecies[comp]] == FLAG) {
								cpdVars[GetVariable(i)->AssociatedSpecies->GetData("DATABASE",STRING)][j][index][compartmentIndecies[comp]] = sign*GetSolution(j)->SolutionData[i];
							} else {
								cpdVars[GetVariable(i)->AssociatedSpecies->GetData("DATABASE",STRING)][j][index][compartmentIndecies[comp]] += sign*GetSolution(j)->SolutionData[i];
							}
						}
						cpdVarTypePresence[j][index][compartmentIndecies[comp]] = true;
					}
				  }
				} else {
					Output << ";NA";
				}
			}

		Output << endl;
	}
	Output.close();
	//Printing the reaction solution data
	Filename.assign(FOutputFilepath());
	Filename.append("MFAOutput/SolutionReactionData");
	Filename.append(FilenameSuffix);
	//Filename.append(itoa(ProblemIndex));
	Filename.append(".txt");
	if (!OpenOutput(Output,Filename)) {
		return;
	}
	Output << "Reaction;Equation;Reversibility";	
	for (int j=StartIndex; j < EndIndex; j++) {
		if (GetSolution(j)->Status == SUCCESS) {
			for (int i=0; i < int(rxnVarTypePresence[j].size()); i++) {
				for (int k=0; k < int(rxnVarTypePresence[j][i].size()); k++) {
					if (rxnVarTypePresence[j][i][k]) {
						if (i == 0) {
							Output << ";Flux";
						} else if (i == 1) {
							Output << ";DeltaG";
						} else if (i == 2) {
							Output << ";DeltaGErr";
						}
						if (k > 0) {
							Output << "[" << compartments[k] << "]";
						}
						if ((EndIndex-StartIndex) > 1) {
							Output << " " << j;
						}
					}
				}
			}
		}
	}
	Output << endl;
	Output << "Solution;none;none";
	for (int j=StartIndex; j < EndIndex; j++) {
		if (GetSolution(j)->Status == SUCCESS) {
			for (int i=0; i < int(rxnVarTypePresence[j].size()); i++) {
				for (int k=0; k < int(rxnVarTypePresence[j][i].size()); k++) {
					if (rxnVarTypePresence[j][i][k]) {
						Output << ";" << j;
					}
				}
			}
		}
	}
	Output << endl;
	Output << "Objective;none;none";
	for (int j=StartIndex; j < EndIndex; j++) {
		if (GetSolution(j)->Status == SUCCESS) {
			for (int i=0; i < int(rxnVarTypePresence[j].size()); i++) {
				for (int k=0; k < int(rxnVarTypePresence[j][i].size()); k++) {
					if (rxnVarTypePresence[j][i][k]) {
						Output << ";" << GetSolution(j)->Objective;
					}
				}
			}
		}
	}
	Output << endl;
	//Printing the actual reaction solution data
	for (int i=0; i < int(reactions.size()); i++) {
		string rev("<=>");
		if (reactions[i]->FType() == FORWARD) {
			rev.assign("=>");
		} else if (reactions[i]->FType() == REVERSE) {
			rev.assign("<=");
		}
		Output << reactions[i]->GetData("DATABASE",STRING) << ";" << reactions[i]->Query("DEFINITION") << ";" << rev; 
		for (int j=StartIndex; j < EndIndex; j++) {
			if (GetSolution(j)->Status == SUCCESS) {
				for (int m=0; m < int(rxnVarTypePresence[j].size()); m++) {
					for (int k=0; k < int(rxnVarTypePresence[j][m].size()); k++) {
						if (rxnVarTypePresence[j][m][k]) {
							string id = reactions[i]->GetData("DATABASE",STRING);
							if (rxnVars[id].count(j) > 0 && int(rxnVars[id][j].size()) > m && int(rxnVars[id][j][m].size()) > k && rxnVars[id][j][m][k] != FLAG) {
								Output << ";" << rxnVars[id][j][m][k];
							} else {
								Output << ";none";
							}
						}
					}
				}
			}
		}
		Output << endl;
	}
	Output.close();
	//Printing the compound solution data
	Filename.assign(FOutputFilepath());
	Filename.append("MFAOutput/SolutionCompoundData");
	Filename.append(FilenameSuffix);
	//Filename.append(itoa(ProblemIndex));
	Filename.append(".txt");
	if (!OpenOutput(Output,Filename)) {
		return;
	}
	Output << "Compound;Name;Formula";	
	for (int j=StartIndex; j < EndIndex; j++) {
		if (GetSolution(j)->Status == SUCCESS) {
			for (int i=0; i < int(cpdVarTypePresence[j].size()); i++) {
				for (int k=0; k < int(cpdVarTypePresence[j][i].size()); k++) {
					if (cpdVarTypePresence[j][i][k]) {
						if (i == 0) {
							Output << ";Drain";
						} else if (i == 1) {
							Output << ";Concentration";
						} else if (i == 2) {
							Output << ";Potential";
						} else if (i == 3) {
							Output << ";DeltaGFErr";
						}
						if (k > 0) {
							Output << "[" << compartments[k] << "]";
						}
						if ((EndIndex-StartIndex) > 1) {
							Output << " " << j;
						}
					}
				}
			}
		}
	}
	Output << endl;
	Output << "Solution;none;none";
	for (int j=StartIndex; j < EndIndex; j++) {
		if (GetSolution(j)->Status == SUCCESS) {
			for (int i=0; i < int(cpdVarTypePresence[j].size()); i++) {
				for (int k=0; k < int(cpdVarTypePresence[j][i].size()); k++) {
					if (cpdVarTypePresence[j][i][k]) {
						Output << ";" << j;
					}
				}
			}
		}
	}
	Output << endl;
	Output << "Objective;none;none";
	for (int j=StartIndex; j < EndIndex; j++) {
		if (GetSolution(j)->Status == SUCCESS) {
			for (int i=0; i < int(cpdVarTypePresence[j].size()); i++) {
				for (int k=0; k < int(cpdVarTypePresence[j][i].size()); k++) {
					if (cpdVarTypePresence[j][i][k]) {
						Output << ";" << GetSolution(j)->Objective;
					}
				}
			}
		}
	}
	Output << endl;
	//Printing the actual reaction solution data
	for (int i=0; i < int(compounds.size()); i++) {
		Output << compounds[i]->GetData("DATABASE",STRING) << ";" << compounds[i]->GetData("NAME",STRING) << ";" << compounds[i]->FFormula(); 
		for (int j=StartIndex; j < EndIndex; j++) {
			if (GetSolution(j)->Status == SUCCESS) {
				for (int m=0; m < int(cpdVarTypePresence[j].size()); m++) {
					for (int k=0; k < int(cpdVarTypePresence[j][m].size()); k++) {
						if (cpdVarTypePresence[j][m][k]) {
							string id = compounds[i]->GetData("DATABASE",STRING);
							if (cpdVars[id].count(j) > 0 && int(cpdVars[id][j].size()) > m && int(cpdVars[id][j][m].size()) > k && cpdVars[id][j][m][k] != FLAG) {
								Output << ";" << cpdVars[id][j][m][k];
							} else {
								Output << ";none";
							}
						}
					}
				}
			}
		}
		Output << endl;
	}
	Output.close();
}

void MFAProblem::PrintVariableKey() {
	if (GetParameter("write variable key").compare("1") == 0) {
		ofstream Output;

		if (!OpenOutput(Output,FOutputFilepath()+GetParameter("MFA variable key filename"))) {
			return;
		}

		Output << "Variable index;Variable type;Variable ID;Variable name;Variable data;Upper bound;Lower bound;Max;Min" << endl;
		for (int i=0; i < FNumVariables(); i++) {
			Output << i+1 << ";" << ConvertVariableType(GetVariable(i)->Type) << ";";
			if (GetVariable(i)->AssociatedSpecies != NULL) {
				Output << GetVariable(i)->AssociatedSpecies->GetData("DATABASE",STRING) << ";";
				if (GetVariable(i)->AssociatedSpecies->GetData("NAME",STRING).length() > 0) {
					Output << GetVariable(i)->AssociatedSpecies->GetData("NAME",STRING) << ";";
				} else {
					Output << GetVariable(i)->AssociatedSpecies->GetData("DATABASE",STRING) << ";";
				}				
				if (GetVariable(i)->Compartment != -1 && GetCompartment(GetVariable(i)->Compartment) != NULL) {
					Output << GetCompartment(GetVariable(i)->Compartment)->Name << ";";
				} else {
					Output << "no compartment;";
				}
			} else if (GetVariable(i)->AssociatedReaction != NULL) {
				Output << GetVariable(i)->AssociatedReaction->GetData("DATABASE",STRING) << ";";
				string EquationType("NAME");
				if (GetVariable(i)->AssociatedReaction->GetData("NAME",STRING).length() > 0) {
					Output << GetVariable(i)->AssociatedReaction->GetData("NAME",STRING) << ";" << GetVariable(i)->AssociatedReaction->CreateReactionEquation(EquationType) << ";";
				} else {
					Output << GetVariable(i)->AssociatedReaction->GetData("DATABASE",STRING) << ";" << GetVariable(i)->AssociatedReaction->CreateReactionEquation(EquationType) << ";";
				}
			} else {
				Output << GetVariable(i)->Name << ";none;";
			}
			Output << GetVariable(i)->UpperBound << ";" << GetVariable(i)->LowerBound << ";" << GetVariable(i)->Max << ";" << GetVariable(i)->Min << endl;
		}

		Output << "Constraint index;Constraint type;Constaint ID;Constraint name;Constraint data;Equality type;RHS" << endl;
		for (int i=0; i < FNumConstraints(); i++) {
			Output << i+1 << ";" << GetConstraint(i)->ConstraintMeaning << ";";
			if (GetConstraint(i)->AssociatedSpecies != NULL) {
				Output << GetConstraint(i)->AssociatedSpecies->GetData("DATABASE",STRING) << ";";
				if (GetConstraint(i)->AssociatedSpecies->GetData("NAME",STRING).length() > 0) {
					Output << GetConstraint(i)->AssociatedSpecies->GetData("NAME",STRING) << ";;";
				} else {
					Output << GetConstraint(i)->AssociatedSpecies->GetData("DATABASE",STRING) << ";;";
				}
			} else if (GetConstraint(i)->AssociatedReaction != NULL) {
				string EquationType("name");
				Output << GetConstraint(i)->AssociatedReaction->GetData("DATABASE",STRING) << ";";
				if (GetConstraint(i)->AssociatedReaction->GetData("NAME",STRING).length() > 0) {
					Output << GetConstraint(i)->AssociatedReaction->GetData("NAME",STRING) << ";" << GetConstraint(i)->AssociatedReaction->CreateReactionEquation(EquationType) << ";";
				} else {
					Output << GetConstraint(i)->AssociatedReaction->GetData("DATABASE",STRING) << ";" << GetConstraint(i)->AssociatedReaction->CreateReactionEquation(EquationType) << ";";
				}
			} else {
				Output << "none;none;";
			}
			Output << GetConstraint(i)->EqualityType << ";" << GetConstraint(i)->RightHandSide << endl;
		}

		Output.close();
	}
}

void MFAProblem::WriteLPFile() {
	PrintVariableKey();
	if (GetParameter("write LP file").compare("1") == 0) {
	  cout << "Writing LP file number " << itoa(lpcount) << endl;
	  GlobalWriteLPFile(Solver,lpcount);
	  lpcount++;
	}
}

void MFAProblem::WriteMFALog() {
  ofstream log_output;
  if (OpenOutput(log_output,FOutputFilepath()+"MFALog.txt")) {
    for (int i = 0; i < MFALog->size(); i++) {
      log_output << (*MFALog)[i].data() << endl;
    }
  }
}
