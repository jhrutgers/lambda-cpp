-- tak benchmark program 
-- Divide-and-conquer structure with tertiary parallelism.
-----------------------------------------------------------------------------

module Main(main) where

import System.Environment (getArgs)
import Control.Parallel

main = do [x,y,z] <- map read `fmap` getArgs
          let g = abs (z-y) + abs (y-x) + abs (z-x)
          let res = partak g x y z
          putStrLn ("tak " ++ show x ++ " " ++ show y ++ " " ++ show z ++ " = " ++ (show res))

partak :: Int -> Int -> Int -> Int -> Int
partak g x y z 
    | x <= y     = z
    | g <= 20    = res
    | otherwise  = x' `par` y' `par` z' `par`
                   res
                   where res = partak (0) x' y' z'
                         x' = partak (g-1) (x-1) y z
                         y' = partak (g-1) (y-1) z x
                         z' = partak (g-1) (z-1) x y
                         -- g =  gran x y z	 
                         -- gran x y z = abs (z-y) + abs (y-x) + abs (z-x)
