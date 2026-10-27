// Fill out your copyright notice in the Description page of Project Settings.


#include "Widgets/Composite/INV_Leaf.h"

void UINV_Leaf::ApplyFunction(FuncType Function)
{
	Function(this);
}
